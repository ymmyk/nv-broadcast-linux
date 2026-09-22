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

}  // namespace nvb
