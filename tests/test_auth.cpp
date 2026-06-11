#include "doctest.h"
#include "auth.h"
#include "util.h"
#include <filesystem>

TEST_CASE("AuthStore 配对密钥增删查") {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / ("dd_auth_" + util::gen_id());
  fs::create_directories(dir);
  {
    AuthStore a(dir / "t.db");
    std::string s;
    CHECK_FALSE(a.get_pairing_secret("p1", s));
    a.save_pairing("p1", "secret1", "甲");
    REQUIRE(a.get_pairing_secret("p1", s));
    CHECK(s == "secret1");
    a.save_pairing("p1", "secret2", "甲");  // 覆盖
    a.get_pairing_secret("p1", s);
    CHECK(s == "secret2");
    a.remove_pairing("p1");
    CHECK_FALSE(a.get_pairing_secret("p1", s));
  }
  fs::remove_all(dir);
}

TEST_CASE("AuthStore 手机注册与查询") {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / ("dd_auth2_" + util::gen_id());
  fs::create_directories(dir);
  {
    AuthStore a(dir / "t.db");
    PhoneInfo p;
    p.id = "ph1"; p.name = "iPhone"; p.token = "tok1"; p.last_seen = 100;
    a.save_phone(p);
    PhoneInfo got;
    REQUIRE(a.find_phone_by_token("tok1", got));
    CHECK(got.id == "ph1");
    CHECK(got.name == "iPhone");
    REQUIRE(a.find_phone_by_id("ph1", got));
    CHECK(got.token == "tok1");
    CHECK_FALSE(a.find_phone_by_token("bad", got));
    a.touch_phone("ph1", 999);
    a.find_phone_by_id("ph1", got);
    CHECK(got.last_seen == 999);
    // 重新注册换 token
    p.token = "tok2";
    a.save_phone(p);
    CHECK_FALSE(a.find_phone_by_token("tok1", got));
    REQUIRE(a.find_phone_by_token("tok2", got));
    CHECK(a.phones().size() == 1);
  }
  fs::remove_all(dir);
}
