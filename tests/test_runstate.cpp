#include "doctest.h"
#include "runstate.h"
#include "util.h"
#include <filesystem>
#include <fstream>

TEST_CASE("runstate 读写删 run.json") {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / ("dd_run_" + util::gen_id());
  fs::create_directories(dir);
  auto f = dir / "run.json";

  CHECK(runstate::read_port(f) == 0);   // 文件不存在 → 0
  runstate::write(f, 8527);
  CHECK(runstate::read_port(f) == 8527);
  runstate::remove(f);
  CHECK(runstate::read_port(f) == 0);   // 删除后 → 0

  std::ofstream(f) << "这不是 json";     // 垃圾内容
  CHECK(runstate::read_port(f) == 0);   // 解析失败 → 0
  fs::remove_all(dir);
}
