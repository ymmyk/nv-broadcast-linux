#pragma once
#include <string>
#include <vector>

#include "common/config.h"

namespace nvb {

// Scan PipeWire/Pulse sources without needing the daemon running.
// Tries `pw-dump`, falls back to `pactl list sources short`.
std::vector<AudioDevice> ScanInputDevices();
void PrintDevices(const std::vector<AudioDevice>& devs);

}  // namespace nvb
