#include "doctest.h"
#include "config.h"
#include "util.h"
#include <filesystem>
#include <fstream>

TEST_CASE("Config 首次生成默认值并持久化设备ID") {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / ("dd_cfg_" + util::gen_id());
  auto c1 = Config::load(dir);
  CHECK(c1.id.size() == 16);
  CHECK(c1.port == 8520);
  CHECK(!c1.name.empty());
  CHECK(fs::exists(dir / "config.json"));
  // 第二次加载得到同一个ID
  auto c2 = Config::load(dir);
  CHECK(c2.id == c1.id);
  CHECK(c2.name == c1.name);
  // 派生路径
  CHECK(c1.db_path() == dir / "history.db");
  CHECK(c1.staging_dir() == dir / "staging");
  fs::remove_all(dir);
}

TEST_CASE("Config 对类型错误的 config.json 降级到默认值而不崩溃") {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / ("dd_cfg_bad_" + util::gen_id());
  fs::create_directories(dir);
  std::ofstream(dir / "config.json")
      << R"({"id": 123, "name": 456, "port": "not-a-number", "download_dir": 789})";
  Config c;
  CHECK_NOTHROW(c = Config::load(dir));
  CHECK(c.id.size() == 16);   // 回退到新生成的ID
  CHECK(c.port == 8520);      // 回退到默认端口
  fs::remove_all(dir);
}
