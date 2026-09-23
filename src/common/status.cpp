#include "common/status.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

namespace nvb {

std::string StatusPath() {
  const char* rt = std::getenv("XDG_RUNTIME_DIR");
  if (rt && *rt) return std::string(rt) + "/nv-broadcast-status.json";
  return "/tmp/nv-broadcast-" + std::to_string(getuid()) + ".json";
}

bool WriteStatus(const std::string& path, const DaemonStatus& st) {
  // Atomic publish: write temp, rename over the live file.
  std::string tmp = path + ".tmp";
  {
    std::ofstream f(tmp, std::ios::trunc);
    if (!f) return false;
    f << "{\"running\":" << (st.running ? "true" : "false")
      << ",\"in_peak\":" << st.in_peak << ",\"out_peak\":" << st.out_peak
      << ",\"captured\":" << st.captured << ",\"rendered\":" << st.rendered
      << ",\"dropped\":" << st.dropped << "}\n";
    f.flush();
    if (!f) return false;
  }
  return std::rename(tmp.c_str(), path.c_str()) == 0;
}

namespace {
double Num(const std::string& json, const char* key) {
  auto p = json.find(key);
  if (p == std::string::npos) return 0;
  p = json.find(':', p);
  if (p == std::string::npos) return 0;
  return std::stod(json.substr(p + 1));
}
}  // namespace

bool ReadStatus(const std::string& path, DaemonStatus* st, long* age_ms) {
  struct stat sb;
  if (stat(path.c_str(), &sb) != 0) return false;
  if (age_ms) {
    struct timeval now;
    gettimeofday(&now, nullptr);
    *age_ms = (now.tv_sec - sb.st_mtime) * 1000L;
  }
  std::ifstream f(path);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  std::string json = ss.str();
  if (json.find("\"running\":true") == std::string::npos &&
      json.find("\"running\": true") == std::string::npos)
    st->running = false;
  else
    st->running = true;
  st->in_peak = (float)Num(json, "\"in_peak\"");
  st->out_peak = (float)Num(json, "\"out_peak\"");
  st->captured = (uint64_t)Num(json, "\"captured\"");
  st->rendered = (uint64_t)Num(json, "\"rendered\"");
  st->dropped = (uint64_t)Num(json, "\"dropped\"");
  return true;
}

}  // namespace nvb
