#include "util.h"
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <random>
#include <sstream>

namespace util {

std::string gen_id() {
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uint64_t> d;
  std::ostringstream os;
  os << std::hex << d(gen);
  std::string s = os.str();
  while (s.size() < 16) s = "0" + s;
  return s;
}

std::filesystem::path home_dir() {
  const char* h = std::getenv("HOME");
  if (!h) h = std::getenv("USERPROFILE");
  return h ? std::filesystem::path(h) : std::filesystem::current_path();
}

std::filesystem::path unique_path(const std::filesystem::path& dir,
                                  const std::string& filename) {
  namespace fs = std::filesystem;
  fs::path p = dir / filename;
  if (!fs::exists(p)) return p;
  std::string stem = fs::path(filename).stem().string();
  std::string ext = fs::path(filename).extension().string();
  for (int i = 1;; ++i) {
    fs::path cand = dir / (stem + " (" + std::to_string(i) + ")" + ext);
    if (!fs::exists(cand)) return cand;
  }
}

std::string url_encode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

long long now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace util
