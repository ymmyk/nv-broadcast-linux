#include "common/config.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace nvb {

std::string AppConfig::default_path() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  const char* home = std::getenv("HOME");
  std::string base = xdg ? xdg : (home ? std::string(home) + "/.config" : ".");
  return base + "/nv-broadcast/config.toml";
}

namespace {
std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace((unsigned char)s[a])) ++a;
  while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
  return s.substr(a, b - a);
}
std::string unquote(std::string s) {
  s = trim(s);
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
    return s.substr(1, s.size() - 2);
  return s;
}
}  // namespace

bool AppConfig::load(const std::string& path, std::string* err) {
  std::ifstream f(path);
  if (!f) return true;  // missing file -> defaults
  std::string section, line;
  while (std::getline(f, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    if (line.front() == '[' && line.back() == ']') {
      section = trim(line.substr(1, line.size() - 2));
      continue;
    }
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = trim(line.substr(0, eq));
    std::string v = trim(line.substr(eq + 1));
    // strip trailing comment
    auto hash = v.find(" #");
    if (hash != std::string::npos) v = trim(v.substr(0, hash));
    try {
      if (section.empty()) {
        if (k == "input_device") input_device = unquote(v);
        else if (k == "output_name") output_name = unquote(v);
        else if (k == "sample_rate") sample_rate = std::stoi(v);
        else if (k == "frame_ms") frame_ms = std::stoi(v);
        else if (k == "channels") channels = std::stoi(v);
      } else if (section == "denoise") {
        if (k == "backend") backend = unquote(v);
        else if (k == "suppression_db") suppression_db = std::stof(v);
        else if (k == "vad_threshold") vad_threshold = std::stof(v);
        else if (k == "keyboard_boost") keyboard_boost = std::stof(v);
      } else if (section == "cuda") {
        if (k == "device_id") cuda_device_id = std::stoi(v);
        else if (k == "model") cuda_model = unquote(v);
      } else if (section == "maxine") {
        if (k == "lib") maxine_lib = unquote(v);
        else if (k == "model") maxine_model = unquote(v);
        else if (k == "effect") maxine_effect = unquote(v);
        else if (k == "enable_vad")
          maxine_enable_vad = (v == "true" || v == "1");
      }
    } catch (...) {
      if (err) *err = "bad value for " + k;
      return false;
    }
  }
  return true;
}

bool AppConfig::save(const std::string& path, std::string* err) const {
  std::ofstream f(path);
  if (!f) {
    if (err) *err = "cannot write " + path;
    return false;
  }
  f << "# managed by nvbcast. See config.example.toml.\n"
    << "input_device  = \"" << input_device << "\"\n"
    << "output_name   = \"" << output_name << "\"\n"
    << "sample_rate   = " << sample_rate << "\n"
    << "frame_ms      = " << frame_ms << "\n"
    << "channels      = " << channels << "\n\n"
    << "[denoise]\nbackend         = \"" << backend << "\"\n"
    << "suppression_db  = " << suppression_db << "\n"
    << "vad_threshold   = " << vad_threshold << "\n"
    << "keyboard_boost  = " << keyboard_boost << "\n\n"
    << "[cuda]\ndevice_id = " << cuda_device_id << "\n"
    << "model     = \"" << cuda_model << "\"\n\n"
    << "[maxine]\nlib        = \"" << maxine_lib << "\"\n"
    << "model      = \"" << maxine_model << "\"\n"
    << "effect     = \"" << maxine_effect << "\"\n"
    << "enable_vad = " << (maxine_enable_vad ? "true" : "false") << "\n";
  return true;
}

}  // namespace nvb
