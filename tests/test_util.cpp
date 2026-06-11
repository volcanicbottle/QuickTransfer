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

TEST_CASE("gen_secret 生成32位十六进制") {
  auto s = util::gen_secret();
  CHECK(s.size() == 32);
  CHECK(s.find_first_not_of("0123456789abcdef") == std::string::npos);
  CHECK(s != util::gen_secret());
}

TEST_CASE("get_cookie_value 解析 Cookie 头") {
  CHECK(util::get_cookie_value("qt_token=abc123", "qt_token") == "abc123");
  CHECK(util::get_cookie_value("a=1; qt_token=xyz; b=2", "qt_token") == "xyz");
  CHECK(util::get_cookie_value("a=1;qt_token=noSpace", "qt_token") == "noSpace");
  CHECK(util::get_cookie_value("token=other", "qt_token") == "");
  CHECK(util::get_cookie_value("", "qt_token") == "");
  CHECK(util::get_cookie_value("myqt_token=bad; qt_token=good", "qt_token") == "good");
}
