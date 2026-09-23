// nvbcast: config + device utility. No daemon, no PipeWire link needed.
#include <cstdlib>
#include <iostream>
#include <string>

#include "common/config.h"
#include "common/devices.h"
#include "common/status.h"

namespace {
void Usage() {
  std::cout << "usage: nvbcast [--config PATH] <cmd> [args]\n"
               "  list                      scan sound inputs (pw-dump/pactl)\n"
               "  status                    show current config\n"
               "  get <key>                 input_device|output_name|backend|suppression_db|vad_threshold|keyboard_boost\n"
               "  set-input <device-id>     set filter input (see list)\n"
               "  set <key> <value>         set a config key\n"
               "  enable|disable            systemctl --user enable/disable nv-broadcast.service\n";
}
}  // namespace

int main(int argc, char** argv) {
  std::string cfg_path = nvb::AppConfig::default_path();
  int ai = 1;
  if (ai < argc && std::string(argv[ai]) == "--config") {
    if (ai + 1 >= argc) {
      Usage();
      return 2;
    }
    cfg_path = argv[ai + 1];
    ai += 2;
  }
  if (ai >= argc) {
    Usage();
    return 2;
  }
  std::string cmd = argv[ai++];

  if (cmd == "list") {
    PrintDevices(nvb::ScanInputDevices());
    return 0;
  }

  nvb::AppConfig cfg;
  std::string err;
  if (!cfg.load(cfg_path, &err)) {
    std::cerr << "bad config: " << err << "\n";
    return 2;
  }

  if (cmd == "status") {
    std::cout << "config: " << cfg_path << "\n"
              << "input_device  = " << (cfg.input_device.empty() ? "<default>" : cfg.input_device) << "\n"
              << "output_name   = " << cfg.output_name << "\n"
              << "backend       = " << cfg.backend << "\n"
              << "suppression_db= " << cfg.suppression_db << "\n"
              << "vad_threshold = " << cfg.vad_threshold << "\n"
              << "keyboard_boost= " << cfg.keyboard_boost << "\n";
    nvb::DaemonStatus live;
    long age = 0;
    if (nvb::ReadStatus(nvb::StatusPath(), &live, &age) && live.running &&
        age < 2000) {
      std::cout << "daemon        = running (in " << live.in_peak << " / out "
                << live.out_peak << ", captured " << live.captured
                << ", rendered " << live.rendered << ", dropped "
                << live.dropped << ")\n";
    } else {
      std::cout << "daemon        = stopped\n";
    }
    return 0;
  }
  if (cmd == "get") {
    if (ai >= argc) {
      Usage();
      return 2;
    }
    std::string k = argv[ai];
    if (k == "input_device") std::cout << cfg.input_device << "\n";
    else if (k == "output_name") std::cout << cfg.output_name << "\n";
    else if (k == "backend") std::cout << cfg.backend << "\n";
    else if (k == "suppression_db") std::cout << cfg.suppression_db << "\n";
    else if (k == "vad_threshold") std::cout << cfg.vad_threshold << "\n";
    else if (k == "keyboard_boost") std::cout << cfg.keyboard_boost << "\n";
    else {
      std::cerr << "unknown key\n";
      return 2;
    }
    return 0;
  }
  if (cmd == "set-input" || cmd == "set") {
    if (cmd == "set-input") {
      if (ai >= argc) {
        Usage();
        return 2;
      }
      cfg.input_device = argv[ai];
    } else {
      if (ai + 1 >= argc) {
        Usage();
        return 2;
      }
      std::string k = argv[ai], v = argv[ai + 1];
      try {
        if (k == "input_device") cfg.input_device = v;
        else if (k == "output_name") cfg.output_name = v;
        else if (k == "backend") cfg.backend = v;
        else if (k == "suppression_db") cfg.suppression_db = std::stof(v);
        else if (k == "vad_threshold") cfg.vad_threshold = std::stof(v);
        else if (k == "keyboard_boost") cfg.keyboard_boost = std::stof(v);
        else if (k == "cuda_device_id") cfg.cuda_device_id = std::stoi(v);
        else if (k == "cuda_model") cfg.cuda_model = v;
        else {
          std::cerr << "unknown key\n";
          return 2;
        }
      } catch (...) {
        std::cerr << "bad value\n";
        return 2;
      }
    }
    if (!cfg.save(cfg_path, &err)) {
      std::cerr << err << "\n";
      return 1;
    }
    std::cout << "saved " << cfg_path << "\n";
    return 0;
  }
  if (cmd == "enable" || cmd == "disable") {
    std::string op = cmd == "enable" ? "enable --now" : "disable --now";
    return std::system(("systemctl --user " + op + " nv-broadcast.service").c_str());
  }
  Usage();
  return 2;
}
