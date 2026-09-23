// Focused regression test for the capture -> denoise -> source frame path.
// No framework: plain asserts via CTest. Runs without a PipeWire server.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "common/config.h"
#include "common/status.h"
#include "denoiser.h"

namespace {

bool Finite(float v) { return std::isfinite(v); }

void TestRnnoiseSilence() {
  std::unique_ptr<nvb::Denoiser> fx(nvb::CreateRnnoiseDenoiser());
  assert(fx->init(48000, nvb::DenoiseConfig{}));
  std::vector<float> frame(480, 0.0f);
  float vad = fx->process(frame.data(), 480);
  assert(vad >= 0.0f && vad <= 1.0f);
  for (float v : frame) assert(Finite(v));
  printf("TestRnnoiseSilence ok (vad=%.3f)\n", vad);
}

void TestRnnoiseTwoFrames() {
  std::unique_ptr<nvb::Denoiser> fx(nvb::CreateRnnoiseDenoiser());
  assert(fx->init(48000, nvb::DenoiseConfig{}));
  std::vector<float> buf(960);
  for (int i = 0; i < 960; ++i) buf[i] = 0.1f * std::sin(i * 0.05f);
  float vad = fx->process(buf.data(), 960);
  assert(vad >= 0.0f && vad <= 1.0f);
  for (float v : buf) assert(Finite(v));
  printf("TestRnnoiseTwoFrames ok (vad=%.3f)\n", vad);
}

void TestCudaStubPassthrough() {
  std::unique_ptr<nvb::Denoiser> fx(nvb::CreateCudaDenoiser(0, "deepfilternet"));
  assert(fx->init(48000, nvb::DenoiseConfig{}));
  std::vector<float> buf(480, 0.25f);
  fx->process(buf.data(), 480);
  for (float v : buf) assert(v == 0.25f);  // stub must not alter audio
  printf("TestCudaStubPassthrough ok\n");
}

void TestConfigRoundTrip() {
  nvb::AppConfig cfg;
  cfg.input_device = "alsa_input.usb-MOTU_M2_M2MA0E94X9-00.HiFi__Mic1__source";
  cfg.backend = "rnnoise";
  cfg.suppression_db = 30.0f;
  const std::string path = "/tmp/nvb-test-roundtrip.toml";
  std::string err;
  assert(cfg.save(path, &err));
  nvb::AppConfig re;
  assert(re.load(path, &err));
  assert(re.input_device == cfg.input_device);
  assert(re.backend == "rnnoise");
  assert(re.suppression_db == 30.0f);
  printf("TestConfigRoundTrip ok\n");
}

void TestStatusRoundTrip() {
  nvb::DaemonStatus st;
  st.running = true;
  st.in_peak = 0.5f;
  st.out_peak = 0.25f;
  st.captured = 48000;
  st.rendered = 47520;
  st.dropped = 480;
  const std::string path = "/tmp/nvb-test-status.json";
  assert(nvb::WriteStatus(path, st));
  nvb::DaemonStatus back;
  long age = -1;
  assert(nvb::ReadStatus(path, &back, &age));
  assert(back.running);
  assert(std::fabs(back.in_peak - 0.5f) < 1e-6);
  assert(std::fabs(back.out_peak - 0.25f) < 1e-6);
  assert(back.captured == 48000 && back.rendered == 47520);
  assert(back.dropped == 480);
  assert(age >= 0 && age < 2000);
  nvb::DaemonStatus none;
  assert(!nvb::ReadStatus("/tmp/nvb-test-status-missing.json", &none));
  printf("TestStatusRoundTrip ok\n");
}

}  // namespace

int main() {
  TestRnnoiseSilence();
  TestRnnoiseTwoFrames();
  TestCudaStubPassthrough();
  TestConfigRoundTrip();
  TestStatusRoundTrip();
  printf("ALL TESTS PASSED\n");
  return 0;
}
