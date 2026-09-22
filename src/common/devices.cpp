#include "common/devices.h"

#include <array>
#include <cstdio>
#include <cstdio>
#include <iostream>
#include <memory>
#include <sstream>

namespace nvb {
namespace {

std::string Exec(const std::string& cmd) {
  std::array<char, 4096> buf{};
  std::string out;
  std::unique_ptr<FILE, decltype(&pclose)> p(popen(cmd.c_str(), "r"), pclose);
  if (!p) return out;
  while (fgets(buf.data(), (int)buf.size(), p.get()))
    out += buf.data();
  return out;
}

// Minimal JSON-ish scrape of `pw-dump` for audio source nodes.
// Full parsing intentionally avoided (no json dep): we grep for
// node.name + media.class = Audio/Source.
std::vector<AudioDevice> FromPwDump(const std::string& dump) {
  std::vector<AudioDevice> devs;
  std::istringstream in(dump);
  std::string line, name, cls, desc;
  auto flush = [&] {
    if (!name.empty() && cls.find("Audio/Source") != std::string::npos) {
      AudioDevice d;
      d.id = name;
      d.label = desc.empty() ? name : desc;
      d.is_input = true;
      devs.push_back(d);
    }
    name.clear();
    cls.clear();
    desc.clear();
  };
  while (std::getline(in, line)) {
    auto pos = line.find("\"node.name\"");
    if (pos != std::string::npos) {
      auto q1 = line.find('"', pos + 12), q2 = line.find('"', q1 + 1);
      if (q1 != std::string::npos && q2 != std::string::npos)
        name = line.substr(q1 + 1, q2 - q1 - 1);
    }
    pos = line.find("\"media.class\"");
    if (pos != std::string::npos) {
      auto q1 = line.find('"', pos + 14), q2 = line.find('"', q1 + 1);
      if (q1 != std::string::npos && q2 != std::string::npos)
        cls = line.substr(q1 + 1, q2 - q1 - 1);
    }
    pos = line.find("\"node.description\"");
    if (pos != std::string::npos) {
      auto q1 = line.find('"', pos + 19), q2 = line.find('"', q1 + 1);
      if (q1 != std::string::npos && q2 != std::string::npos)
        desc = line.substr(q1 + 1, q2 - q1 - 1);
    }
    if (line.find("},") != std::string::npos || line.find("}") == 0) {
      if (!name.empty()) flush();
    }
  }
  if (!name.empty()) flush();
  return devs;
}

}  // namespace

std::vector<AudioDevice> ScanInputDevices() {
  std::string dump = Exec("pw-dump 2>/dev/null");
  if (!dump.empty()) {
    auto devs = FromPwDump(dump);
    if (!devs.empty()) return devs;
  }
  // Fallback: pactl (pipewire-pulse) short list: "<idx>\t<name>\t..."
  std::string pactl = Exec("pactl list sources short 2>/dev/null");
  std::vector<AudioDevice> devs;
  std::istringstream in(pactl);
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream row(line);
    std::string idx, name;
    if (!(row >> idx >> name)) continue;
    if (name.find(".monitor") != std::string::npos) continue;
    devs.push_back({name, name, true});
  }
  return devs;
}

void PrintDevices(const std::vector<AudioDevice>& devs) {
  if (devs.empty()) {
    std::cout << "No input devices found. Is PipeWire running?\n"
                 "Try (host terminal): systemctl --user status pipewire\n";
    return;
  }
  for (auto& d : devs) std::cout << "  " << d.id << "  # " << d.label << "\n";
}

}  // namespace nvb
