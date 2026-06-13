#include "runstate.h"
#include <fstream>
#include "json.hpp"

namespace runstate {

void write(const std::filesystem::path& file, int port) {
  std::ofstream(file) << nlohmann::json{{"port", port}}.dump();
}

int read_port(const std::filesystem::path& file) {
  std::error_code ec;
  if (!std::filesystem::exists(file, ec)) return 0;
  std::ifstream in(file);
  auto j = nlohmann::json::parse(in, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return 0;
  try {
    return j.value("port", 0);
  } catch (const nlohmann::json::exception&) {
    return 0;
  }
}

void remove(const std::filesystem::path& file) {
  std::error_code ec;
  std::filesystem::remove(file, ec);
}

}  // namespace runstate
