// nvbcastd: capture -> RNNoise denoise -> virtual PipeWire source.
//
// Graph: capture pw_stream (INPUT) on cfg.input_device (or default source)
// -> ring buffer -> source pw_stream (OUTPUT, media.class=Audio/Source,
// node.name=cfg.output_name) that apps select as their mic. Both streams
// negotiate F32/48kHz/mono; PipeWire resamples/downmixes the hardware side
// automatically. Denoiser runs in 480-sample (10 ms) frames.
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <unistd.h>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "common/config.h"
#include "denoiser.h"

#ifdef NVB_HAVE_PIPEWIRE
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#endif

namespace {
volatile std::sig_atomic_t g_stop = 0;
void OnSig(int) {
  // First Ctrl-C: graceful stop. Second: force out (never hang the terminal).
  if (++g_stop > 1) _exit(130);
}
}  // namespace

#ifdef NVB_HAVE_PIPEWIRE
namespace {

constexpr int kRate = 48000;
constexpr int kFrame = 480;  // RNNoise-native 10 ms @ 48 kHz
constexpr size_t kRingMax = 48000 * 5;  // 5 s backpressure cap

struct Graph {
  nvb::Denoiser* fx = nullptr;
  std::mutex mu;
  std::deque<float> ring;      // captured samples awaiting denoise
  std::vector<float> pending;  // denoised-but-unwritten spillover
  uint64_t captured = 0, rendered = 0, dropped = 0;
};

const struct spa_pod* BuildF32Mono(struct spa_pod_builder* b) {
  struct spa_audio_info_raw info;
  info.format = SPA_AUDIO_FORMAT_F32;
  info.flags = 0;
  info.rate = kRate;
  info.channels = 1;
  info.position[0] = SPA_AUDIO_CHANNEL_MONO;
  return spa_format_audio_raw_build(b, SPA_PARAM_EnumFormat, &info);
}

void OnCaptureProcess(void* data) {
  auto* g = static_cast<Graph*>(data);
  extern pw_stream* g_cap;  // set in Run(); avoids extra context struct
  struct pw_buffer* pw = pw_stream_dequeue_buffer(g_cap);
  if (!pw) return;
  struct spa_buffer* buf = pw->buffer;
  float* samples = nullptr;
  uint32_t n = 0;
  if (buf && buf->datas[0].data) {
    samples = static_cast<float*>(buf->datas[0].data);
    uint32_t bytes = buf->datas[0].chunk->size;
    if (bytes > (uint32_t)buf->datas[0].maxsize) bytes = buf->datas[0].maxsize;
    n = bytes / sizeof(float);
  }
  if (samples && n) {
    std::lock_guard<std::mutex> lk(g->mu);
    for (uint32_t i = 0; i < n; ++i) g->ring.push_back(samples[i]);
    g->captured += n;
    while (g->ring.size() > kRingMax) {
      g->ring.pop_front();
      ++g->dropped;
    }
  }
  pw_stream_queue_buffer(g_cap, pw);
}

pw_stream* g_cap = nullptr;
pw_stream* g_src = nullptr;

void OnSourceProcess(void* data) {
  auto* g = static_cast<Graph*>(data);
  struct pw_buffer* pw = pw_stream_dequeue_buffer(g_src);
  if (!pw) return;
  struct spa_buffer* buf = pw->buffer;
  float* out = nullptr;
  uint32_t capacity = 0;
  if (buf && buf->datas[0].data) {
    out = static_cast<float*>(buf->datas[0].data);
    capacity = buf->datas[0].maxsize / sizeof(float);
  }
  uint32_t wrote = 0;
  if (out && capacity) {
    std::lock_guard<std::mutex> lk(g->mu);
    // Drain ring into pending, then emit full 480-sample denoised frames.
    while (!g->ring.empty()) {
      g->pending.push_back(g->ring.front());
      g->ring.pop_front();
    }
    size_t avail = g->pending.size();
    size_t off = 0;
    while (off + kFrame <= avail && wrote + kFrame <= capacity) {
      g->fx->process(g->pending.data() + off, kFrame);
      for (int i = 0; i < kFrame; ++i) out[wrote++] = g->pending[off + i];
      off += kFrame;
    }
    if (off) g->pending.erase(g->pending.begin(), g->pending.begin() + off);
    // Underrun: pad with silence (mic idle / capture catching up).
    while (wrote < capacity) out[wrote++] = 0.0f;
    g->rendered += wrote;
    buf->datas[0].chunk->offset = 0;
    buf->datas[0].chunk->stride = sizeof(float);
    buf->datas[0].chunk->size = wrote * sizeof(float);
  }
  pw_stream_queue_buffer(g_src, pw);
}

void OnStateChanged(void* data, enum pw_stream_state old_state,
                    enum pw_stream_state state, const char* error) {
  (void)data;
  (void)old_state;
  if (error)
    std::cerr << "nvbcastd: stream error: " << error << "\n";
  else if (state == PW_STREAM_STATE_STREAMING)
    std::cout << "nvbcastd: streaming\n";
}

const struct pw_stream_events kCaptureEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = OnStateChanged,
    .process = OnCaptureProcess,
};
const struct pw_stream_events kSourceEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = OnStateChanged,
    .process = OnSourceProcess,
};

int Run(const nvb::AppConfig& cfg, nvb::Denoiser* fx) {
  pw_init(nullptr, nullptr);
  Graph g;
  g.fx = fx;
  g.pending.reserve(kFrame * 4);

  pw_thread_loop* loop = pw_thread_loop_new("nv-broadcast", nullptr);
  if (!loop) {
    std::cerr << "nvbcastd: pw_thread_loop_new failed\n";
    return 1;
  }
  pw_loop* pwloop = pw_thread_loop_get_loop(loop);

  pw_thread_loop_lock(loop);

  pw_properties* cap_props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
      PW_KEY_MEDIA_ROLE, "Communication", PW_KEY_NODE_NAME,
      "nv-broadcast-capture", nullptr);
  if (!cfg.input_device.empty())
    pw_properties_set(cap_props, PW_KEY_TARGET_OBJECT, cfg.input_device.c_str());
  g_cap = pw_stream_new_simple(pwloop, "nv-broadcast-capture", cap_props,
                               &kCaptureEvents, &g);
  pw_properties* src_props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CLASS, "Audio/Source",
      PW_KEY_MEDIA_ROLE, "Communication", PW_KEY_NODE_NAME,
      cfg.output_name.c_str(), PW_KEY_NODE_DESCRIPTION,
      "NV Broadcast Mic (denoised)", nullptr);
  g_src = pw_stream_new_simple(pwloop, cfg.output_name.c_str(), src_props,
                               &kSourceEvents, &g);
  if (!g_cap || !g_src) {
    std::cerr << "nvbcastd: stream creation failed\n";
    pw_thread_loop_unlock(loop);
    return 1;
  }

  uint8_t buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
  const struct spa_pod* params[1] = {BuildF32Mono(&b)};

  if (pw_stream_connect(g_cap, PW_DIRECTION_INPUT, PW_ID_ANY,
                        (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT |
                                          PW_STREAM_FLAG_MAP_BUFFERS |
                                          PW_STREAM_FLAG_RT_PROCESS),
                        params, 1) != 0) {
    std::cerr << "nvbcastd: capture connect failed\n";
    pw_thread_loop_unlock(loop);
    return 1;
  }
  // Rebuild POD (builder was consumed).
  b = (struct spa_pod_builder)SPA_POD_BUILDER_INIT(buf, sizeof(buf));
  params[0] = BuildF32Mono(&b);
  if (pw_stream_connect(g_src, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                        (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT |
                                          PW_STREAM_FLAG_MAP_BUFFERS |
                                          PW_STREAM_FLAG_RT_PROCESS),
                        params, 1) != 0) {
    std::cerr << "nvbcastd: source connect failed\n";
    pw_thread_loop_unlock(loop);
    return 1;
  }

  pw_thread_loop_start(loop);
  pw_thread_loop_unlock(loop);

  std::cout << "nvbcastd: running (Ctrl-C to stop)\n";
  while (!g_stop) std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // stop() joins the loop thread, so it must run WITHOUT the loop lock
  // (the loop thread needs that lock to finish its current callback).
  // Locking around stop() was the Ctrl-C deadlock.
  pw_thread_loop_stop(loop);
  pw_thread_loop_lock(loop);
  if (g_cap) pw_stream_destroy(g_cap);
  if (g_src) pw_stream_destroy(g_src);
  g_cap = g_src = nullptr;
  pw_thread_loop_unlock(loop);
  pw_thread_loop_destroy(loop);
  pw_deinit();

  std::lock_guard<std::mutex> lk(g.mu);
  std::cout << "nvbcastd: captured=" << g.captured
            << " rendered=" << g.rendered << " dropped=" << g.dropped << "\n";
  return 0;
}

}  // namespace
#endif  // NVB_HAVE_PIPEWIRE

int main(int argc, char** argv) {
  std::string cfg_path = nvb::AppConfig::default_path();
  if (argc > 1) cfg_path = argv[1];

  nvb::AppConfig cfg;
  std::string err;
  if (!cfg.load(cfg_path, &err)) {
    std::cerr << "bad config " << cfg_path << ": " << err << "\n";
    return 2;
  }

  std::signal(SIGINT, OnSig);
  std::signal(SIGTERM, OnSig);

  nvb::DenoiseConfig dcfg{cfg.suppression_db, cfg.vad_threshold,
                          cfg.keyboard_boost};
  std::unique_ptr<nvb::Denoiser> fx;
  if (cfg.backend == "cuda")
    fx.reset(nvb::CreateCudaDenoiser(cfg.cuda_device_id, cfg.cuda_model));
  else
    fx.reset(nvb::CreateRnnoiseDenoiser());
  if (!fx->init(cfg.sample_rate, dcfg)) {
    std::cerr << "denoiser init failed (" << fx->name() << ")\n";
    return 1;
  }

  std::cout << "nvbcastd: backend=" << fx->name() << " in='"
            << (cfg.input_device.empty() ? "<default>" : cfg.input_device)
            << "' out='" << cfg.output_name << "'\n"
            << "Select '" << cfg.output_name << "' as mic in your apps.\n";

#ifdef NVB_HAVE_PIPEWIRE
  return Run(cfg, fx.get());
#else
  std::vector<float> frame(480, 0.0f);
  fx->process(frame.data(), 480);
  std::cout << "nvbcastd: pipewire support not compiled in; exiting\n";
  return 0;
#endif
}
