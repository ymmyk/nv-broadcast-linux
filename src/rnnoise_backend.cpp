#include "denoiser.h"

#include <algorithm>
#include <cmath>
#include <memory>

#ifdef NVB_HAVE_RNNOISE
#include <rnnoise.h>
#endif

namespace nvb {
namespace {

class RnnoiseDenoiser : public Denoiser {
 public:
  bool init(int sample_rate, const DenoiseConfig& cfg) override {
    cfg_ = cfg;
    // RNNoise is trained for 48 kHz. Daemon resamples to 48k before calling.
    (void)sample_rate;
#ifdef NVB_HAVE_RNNOISE
    st_.reset(rnnoise_create(nullptr));
    return st_ != nullptr;
#else
    return true;  // passthrough stub when librnnoise missing
#endif
  }

  float process(float* frames, int n) override {
#ifdef NVB_HAVE_RNNOISE
    if (!st_) return 0.0f;
    // RNNoise frame = 480 samples. Gate extra keyboard attenuation by VAD.
    float vad_sum = 0.0f;
    int frames_done = 0;
    for (int off = 0; off + 480 <= n; off += 480) {
      float vad = rnnoise_process_frame(st_.get(), frames + off, frames + off);
      vad_sum += vad;
      ++frames_done;
      if (vad < cfg_.vad_threshold) {
        float gate = 1.0f - (cfg_.suppression_db / 60.0f);
        gate = std::clamp(gate, 0.0f, 1.0f);
        for (int i = 0; i < 480; ++i) frames[off + i] *= gate;
      }
      // keyboard_boost: extra HF transient cut on low-VAD frames.
      if (cfg_.keyboard_boost > 0.0f && vad < cfg_.vad_threshold) {
        float cut = 1.0f - 0.5f * cfg_.keyboard_boost;
        for (int i = 1; i < 480; ++i)
          frames[off + i] = frames[off + i] * cut + frames[off + i - 1] * (1.0f - cut) * 0.25f;
      }
    }
    return frames_done ? vad_sum / frames_done : 0.0f;
#else
    (void)frames;
    (void)n;
    return 0.0f;
#endif
  }

  std::string name() const override {
#ifdef NVB_HAVE_RNNOISE
    return "rnnoise";
#else
    return "rnnoise-passthrough";
#endif
  }

 private:
  DenoiseConfig cfg_;
#ifdef NVB_HAVE_RNNOISE
  std::unique_ptr<DenoiseState, decltype(&rnnoise_destroy)> st_{nullptr, rnnoise_destroy};
#endif
};

}  // namespace

Denoiser* CreateRnnoiseDenoiser() { return new RnnoiseDenoiser(); }

}  // namespace nvb
