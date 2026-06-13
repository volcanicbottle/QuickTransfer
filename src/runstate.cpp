#include "runstate.h"
#include <fstream>
#include "json.hpp"

namespace runstate {

void write(const std::filesystem::path& file, int port) {
  std::ofstream(file) << nlohmann::json{{"port", port}}.dump();
}

int read_port(const std::filesystem::path& file) {
  std::ifstream in(file);  // 缺失文件 → 失败流 → parse 得到 discarded → 返回 0
  auto j = nlohmann::json::parse(in, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return 0;
  try {
    return j.value("port", 0);  // port 字段类型异常（如字符串）时回退 0
  } catch (const nlohmann::json::exception&) {
    return 0;
  }
}

void remove(const std::filesystem::path& file) {
  std::error_code ec;
  std::filesystem::remove(file, ec);
}

}  // namespace runstate
