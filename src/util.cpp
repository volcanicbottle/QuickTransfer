#include "util.h"
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iomanip>
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
    bool unreserved = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                      (c >= 'a' && c <= 'z');
    if (unreserved || c == '-' || c == '_' || c == '.' || c == '~') {
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

std::string gen_secret() {
  // 直接从 random_device 取满 128 位熵——这是配对密钥/手机 token，
  // 不能走 gen_id 的 mt19937（仅 32 位种子，熵不足且输出泄漏可还原状态）
  std::random_device rd;
  std::ostringstream os;
  os << std::hex << std::setfill('0');
  for (int i = 0; i < 4; ++i) os << std::setw(8) << (uint32_t)rd();
  return os.str();
}

std::string get_cookie_value(const std::string& cookie_header, const std::string& name) {
  size_t pos = 0;
  while (pos < cookie_header.size()) {
    size_t end = cookie_header.find(';', pos);
    if (end == std::string::npos) end = cookie_header.size();
    std::string item = cookie_header.substr(pos, end - pos);
    size_t start = item.find_first_not_of(' ');
    if (start != std::string::npos) {
      item = item.substr(start);
      size_t eq = item.find('=');
      if (eq != std::string::npos && item.substr(0, eq) == name)
        return item.substr(eq + 1);
    }
    pos = end + 1;
  }
  return "";
}

}  // namespace util
