#pragma once
#include <string>
#include <vector>

namespace nvb {

struct AppConfig {
  std::string input_device;   // empty = default source
  std::string output_name = "NV-Broadcast-Mic";
  int sample_rate = 48000;
  int frame_ms = 10;
  int channels = 1;
  // [denoise]
  std::string backend = "rnnoise";
  float suppression_db = 24.0f;
  float vad_threshold = 0.5f;
  float keyboard_boost = 0.6f;
  // [cuda]
  int cuda_device_id = 0;
  std::string cuda_model = "deepfilternet";

  static std::string default_path();
  bool load(const std::string& path, std::string* err);
  bool save(const std::string& path, std::string* err) const;
};

struct AudioDevice {
  std::string id;     // pipewire/pulse node name
  std::string label;  // human description
  bool is_input = true;
};

}  // namespace nvb
