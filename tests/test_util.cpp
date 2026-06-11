#include "doctest.h"
#include "util.h"
#include <filesystem>
#include <fstream>

TEST_CASE("gen_id 生成16位十六进制且互不相同") {
  auto a = util::gen_id();
  auto b = util::gen_id();
  CHECK(a.size() == 16);
  CHECK(a != b);
  CHECK(a.find_first_not_of("0123456789abcdef") == std::string::npos);
}

TEST_CASE("unique_path 在重名时自动追加序号") {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / ("dd_test_" + util::gen_id());
  fs::create_directories(dir);
  CHECK(util::unique_path(dir, "a.txt") == dir / "a.txt");
  std::ofstream(dir / "a.txt") << "x";
  CHECK(util::unique_path(dir, "a.txt") == dir / "a (1).txt");
  std::ofstream(dir / "a (1).txt") << "x";
  CHECK(util::unique_path(dir, "a.txt") == dir / "a (2).txt");
  fs::remove_all(dir);
}

TEST_CASE("url_encode 编码非安全字符") {
  CHECK(util::url_encode("abc-123_.~") == "abc-123_.~");
  CHECK(util::url_encode("a b") == "a%20b");
  CHECK(util::url_encode("文") == "%E6%96%87");
}

TEST_CASE("now_ms 返回合理时间戳") {
  CHECK(util::now_ms() > 1700000000000LL);
}
