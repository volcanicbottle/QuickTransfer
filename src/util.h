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
std::string gen_secret();  // 32 位十六进制随机密钥
// 从 HTTP Cookie 头解析指定名字的值；找不到返回空串
std::string get_cookie_value(const std::string& cookie_header, const std::string& name);
}
