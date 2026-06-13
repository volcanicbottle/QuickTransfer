#include "doctest.h"
#include "auth.h"
#include "util.h"
#include <filesystem>
#include <sqlite3.h>

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

TEST_CASE("PinGuard 连续失败10次锁定5分钟") {
  PinGuard g;
  long long t0 = 1000000;
  CHECK_FALSE(g.locked(t0));
  for (int i = 0; i < 9; ++i) g.record_failure(t0);
  CHECK_FALSE(g.locked(t0));        // 9 次还没锁
  g.record_failure(t0);             // 第 10 次
  CHECK(g.locked(t0));
  CHECK(g.locked(t0 + PinGuard::kLockMs - 1));
  CHECK_FALSE(g.locked(t0 + PinGuard::kLockMs));  // 到期解锁
}

TEST_CASE("PinGuard 成功后清零计数") {
  PinGuard g;
  long long t0 = 1000000;
  for (int i = 0; i < 9; ++i) g.record_failure(t0);
  g.record_success();
  for (int i = 0; i < 9; ++i) g.record_failure(t0);
  CHECK_FALSE(g.locked(t0));        // 重新计数，9 次不锁
}

TEST_CASE("PinGuard 锁定期间的失败不延长锁定") {
  PinGuard g;
  long long t0 = 1000000;
  for (int i = 0; i < 10; ++i) g.record_failure(t0);
  CHECK(g.locked(t0));
  // 锁定期间继续疯狂尝试（攻击者行为）
  for (int i = 0; i < 50; ++i) g.record_failure(t0 + 1000);
  CHECK_FALSE(g.locked(t0 + PinGuard::kLockMs));  // 仍按原时间到期，未被延长
}

TEST_CASE("PinGuard 单次失败后进入节流窗口") {
  PinGuard g;
  long long t0 = 1000000;
  CHECK_FALSE(g.throttled(t0));
  g.record_failure(t0);
  CHECK(g.throttled(t0));
  CHECK(g.throttled(t0 + PinGuard::kFailDelayMs - 1));
  CHECK_FALSE(g.throttled(t0 + PinGuard::kFailDelayMs));  // 窗口结束
}

TEST_CASE("PinGuard 锁定到期后重新从零计数") {
  PinGuard g;
  long long t0 = 1000000;
  for (int i = 0; i < 10; ++i) g.record_failure(t0);
  long long after = t0 + PinGuard::kLockMs;  // 已到期
  for (int i = 0; i < 9; ++i) g.record_failure(after);
  CHECK_FALSE(g.locked(after));  // 到期后需再失败 10 次才会重新锁定
  g.record_failure(after);
  CHECK(g.locked(after));
}

TEST_CASE("normalize_device 仅接受 phone/pc") {
  CHECK(normalize_device("pc") == "pc");
  CHECK(normalize_device("phone") == "phone");
  CHECK(normalize_device("") == "phone");
  CHECK(normalize_device("hacker") == "phone");
}

TEST_CASE("AuthStore device 列读写默认值") {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / ("dd_dev_" + util::gen_id());
  fs::create_directories(dir);
  {
    AuthStore a(dir / "t.db");
    PhoneInfo p;
    p.id = "ph1"; p.name = "MacBook"; p.token = "t1"; p.last_seen = 1; p.device = "pc";
    a.save_phone(p);
    PhoneInfo got;
    REQUIRE(a.find_phone_by_id("ph1", got));
    CHECK(got.device == "pc");

    PhoneInfo p2;  // 不设 device，结构体默认 "phone"
    p2.id = "ph2"; p2.name = "iPhone"; p2.token = "t2"; p2.last_seen = 1;
    a.save_phone(p2);
    REQUIRE(a.find_phone_by_id("ph2", got));
    CHECK(got.device == "phone");
  }
  fs::remove_all(dir);
}

TEST_CASE("AuthStore 旧库无 device 列自动迁移") {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / ("dd_mig_" + util::gen_id());
  fs::create_directories(dir);
  auto db = dir / "t.db";
  {  // 用旧 schema 建库并插入一行（无 device 列）
    sqlite3* raw = nullptr;
    REQUIRE(sqlite3_open(db.u8string().c_str(), &raw) == SQLITE_OK);
    sqlite3_exec(raw,
        "CREATE TABLE phones(id TEXT PRIMARY KEY,name TEXT,token TEXT,last_seen INTEGER);"
        "INSERT INTO phones VALUES('old','旧机','tk',5);",
        nullptr, nullptr, nullptr);
    sqlite3_close(raw);
  }
  {
    AuthStore a(db);  // 构造时应 ALTER 补列
    PhoneInfo got;
    REQUIRE(a.find_phone_by_id("old", got));
    CHECK(got.device == "phone");  // 旧记录默认 phone
  }
  fs::remove_all(dir);
}
