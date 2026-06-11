#pragma once
#include <filesystem>
#include <string>

namespace util {
std::string gen_id();                 // 16 位十六进制随机ID
std::filesystem::path home_dir();     // HOME / USERPROFILE
std::filesystem::path unique_path(const std::filesystem::path& dir,
                                  const std::string& filename);  // 重名追加 " (n)"
std::string url_encode(const std::string& s);
long long now_ms();
}
