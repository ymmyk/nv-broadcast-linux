#pragma once
#include <cstdint>
#include <string>

namespace nvb {

// Live daemon telemetry, polled by the GUI and `nvbcast status`.
// The daemon rewrites this file ~5x/sec; readers treat a file older
// than 2 s (or missing) as "daemon stopped".
struct DaemonStatus {
  bool running = false;
  float in_peak = 0.0f;    // 0..1+ input peak since last write
  float out_peak = 0.0f;   // 0..1+ denoised output peak since last write
  uint64_t captured = 0;
  uint64_t rendered = 0;
  uint64_t dropped = 0;
};

std::string StatusPath();
bool WriteStatus(const std::string& path, const DaemonStatus& st);
bool ReadStatus(const std::string& path, DaemonStatus* st,
                long* age_ms = nullptr);

}  // namespace nvb
