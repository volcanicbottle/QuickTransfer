#include "config.h"
#include "json.hpp"
#include "util.h"
#include <fstream>
#include <cstdio>
#include <random>
#ifndef _WIN32
#include <unistd.h>
#endif

static std::string gen_pin() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> d(0, 999999);
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%06d", d(gen));
  return buf;
}

static std::string host_name() {
#ifdef _WIN32
  const char* n = std::getenv("COMPUTERNAME");
  return n ? n : "Windows电脑";
#else
  char buf[256] = {0};
  if (gethostname(buf, sizeof(buf) - 1) == 0 && buf[0]) return buf;
  return "Linux电脑";
#endif
}

Config Config::load(const std::filesystem::path& data_dir) {
  namespace fs = std::filesystem;
  Config c;
  c.data_dir = data_dir;
  c.id = util::gen_id();
  c.name = host_name();
  c.pin = gen_pin();
  c.download_dir = util::home_dir() / "Downloads" / "多端互通";
  fs::create_directories(data_dir);
  auto file = data_dir / "config.json";
  if (fs::exists(file)) {
    std::ifstream in(file);
    auto j = nlohmann::json::parse(in, nullptr, false);
    if (!j.is_discarded()) {
      try {
        c.id = j.value("id", c.id);
        c.name = j.value("name", c.name);
        c.port = j.value("port", c.port);
        c.pin = j.value("pin", c.pin);
        if (j.contains("download_dir"))
          c.download_dir = fs::path(j["download_dir"].get<std::string>());
      } catch (const nlohmann::json::exception&) {
        // 字段类型错误：保留默认值
      }
    }
  }
  c.save();
  return c;
}

void Config::save() const {
  nlohmann::json j{{"id", id},
                   {"name", name},
                   {"port", port},
                   {"pin", pin},
                   {"download_dir", download_dir.string()}};
  std::ofstream(data_dir / "config.json") << j.dump(2);
}
