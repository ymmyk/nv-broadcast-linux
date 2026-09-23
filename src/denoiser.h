#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace nvb {

// 48 kHz mono float32 frames. RNNoise-native: 480 samples (10 ms).
struct DenoiseConfig {
  float suppression_db = 24.0f;
  float vad_threshold = 0.5f;
  float keyboard_boost = 0.6f;  // extra transient suppression 0..1
  // Maxine backend options (see config [maxine]); env $NVB_MAXINE_LIB and
  // $NVB_MAXINE_MODEL override lib/model when set (handy for testing).
  std::string maxine_lib;     // .so path, empty = libnv_audio_effects.so
  std::string maxine_model;   // .trtpkg path or "denoiser_48k" shorthand
  std::string maxine_effect;  // effect selector, default "denoiser"
  bool maxine_enable_vad = true;
};

class Denoiser {
 public:
  virtual ~Denoiser() = default;
  virtual bool init(int sample_rate, const DenoiseConfig& cfg) = 0;
  // In-place denoise of `frames` samples. Returns voice-activity 0..1.
  virtual float process(float* frames, int n) = 0;
  virtual std::string name() const = 0;
};

// Factories (defined per backend).
Denoiser* CreateRnnoiseDenoiser();
Denoiser* CreateCudaDenoiser(int device_id, const std::string& model);
Denoiser* CreateMaxineDenoiser(DenoiseConfig cfg);

}  // namespace nvb
