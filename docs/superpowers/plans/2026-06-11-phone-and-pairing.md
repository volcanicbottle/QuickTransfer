# 手机接入与统一配对 实施计划（第二阶段）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 手机浏览器凭 PIN 注册后与电脑互传文字/文件；电脑↔电脑互传改为 PIN 配对换密钥的强制鉴权。

**Architecture:** 复用第一阶段的 Message/SSE/前端气泡模型。新增 auth 模块（SQLite pairings/phones 表 + PIN 防爆破），server 增加 pre-routing 鉴权边界与配对/手机路由（新 TU `server_auth.cpp`），前端增加登录视图、手机单会话模式、设备分组折叠。

**Tech Stack:** 既有栈不变（C++17、cpp-httplib v0.15.3、SQLite、nlohmann/json、doctest、纯 HTML/JS）。

**设计文档:** `docs/superpowers/specs/2026-06-11-phone-and-pairing-design.md`

**里程碑映射:** M1=任务1–5；M2=任务6；M3=任务7–8；M4=任务9–10；M5=任务11。

---

## 项目惯例（所有任务必读）

- 构建命令：`cmake -B build && cmake --build build -j && ./build/unit_tests`（GLOB+CONFIGURE_DEPENDS，新文件自动收集）
- **JSON 容错惯例**：所有解析请求 body 的地方必须 `json::parse(s, nullptr, false)` + `if (j.is_discarded() || !j.is_object()) → 400` + 取值块包 `try/catch (const nlohmann::json::exception&) → 400`。nlohmann 的 `j.value()` 在字段存在但类型不符时抛异常，本项目踩过三次。
- commit 信息**不加** Co-Authored-By 尾注（用户要求）
- doctest 测试文件只 `#include "doctest.h"`（main 在 test_main.cpp）
- 集成验证用过的后台进程务必 `pkill -f 'build/duoduan'` 清理，临时目录删除

## 文件结构

```
src/auth.h/.cpp          # 新增：AuthStore（pairings/phones 表）+ PinGuard（防爆破）
src/server_auth.cpp      # 新增：App 的配对/手机路由（setup_auth_routes + 辅助方法），与 server.cpp 同类不同 TU
src/util.h/.cpp          # 修改：+gen_secret、+get_cookie_value
src/config.h/.cpp        # 修改：+pin 字段
src/history.cpp          # 修改：+busy_timeout（现在同一 db 文件有两个连接）
src/server.h/.cpp        # 修改：成员 auth_/pin_guard_、pre-routing、既有路由的鉴权与手机分支
web/index.html/style.css/app.js  # 修改：登录视图、手机模式、分组折叠、下载按钮、PIN 显示
tests/test_util.cpp test_config.cpp  # 修改：补新函数测试
tests/test_auth.cpp      # 新增
```

---

### Task 1: util 扩展——gen_secret 与 get_cookie_value（TDD）

**Files:**
- Modify: `src/util.h`, `src/util.cpp`
- Modify: `tests/test_util.cpp`（末尾追加）

- [ ] **Step 1: tests/test_util.cpp 末尾追加失败测试**

```cpp
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
```

- [ ] **Step 2: src/util.h 追加声明，构建确认链接失败**

```cpp
std::string gen_secret();  // 32 位十六进制随机密钥
// 从 HTTP Cookie 头解析指定名字的值；找不到返回空串
std::string get_cookie_value(const std::string& cookie_header, const std::string& name);
```

Run: `cmake -B build && cmake --build build -j 2>&1 | tail -3` → 预期 undefined reference

- [ ] **Step 3: src/util.cpp 实现**

```cpp
std::string gen_secret() { return gen_id() + gen_id(); }

std::string get_cookie_value(const std::string& cookie_header, const std::string& name) {
  size_t pos = 0;
  while (pos < cookie_header.size()) {
    size_t end = cookie_header.find(';', pos);
    if (end == std::string::npos) end = cookie_header.size();
    std::string item = cookie_header.substr(pos, end - pos);
    size_t start = item.find_first_not_of(' ');
    if (start != std::string::npos) {
      item = item.substr(start);
      size_t eq = item.find('=');
      if (eq != std::string::npos && item.substr(0, eq) == name)
        return item.substr(eq + 1);
    }
    pos = end + 1;
  }
  return "";
}
```

- [ ] **Step 4: 构建+测试全过**
- [ ] **Step 5: Commit** `git add -A && git commit -m "feat: util 增加密钥生成与 Cookie 解析"`

---

### Task 2: config 增加 PIN 字段（TDD）

**Files:**
- Modify: `src/config.h`, `src/config.cpp`
- Modify: `tests/test_config.cpp`（末尾追加）

- [ ] **Step 1: tests/test_config.cpp 末尾追加失败测试**

```cpp
TEST_CASE("Config 首次生成6位数字PIN并持久化") {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / ("dd_cfg_pin_" + util::gen_id());
  auto c1 = Config::load(dir);
  CHECK(c1.pin.size() == 6);
  CHECK(c1.pin.find_first_not_of("0123456789") == std::string::npos);
  auto c2 = Config::load(dir);
  CHECK(c2.pin == c1.pin);
  fs::remove_all(dir);
}
```

- [ ] **Step 2: src/config.h 的 Config 结构体加字段（port 之后）**

```cpp
  std::string pin;  // 6 位数字配对 PIN，首启生成
```

构建确认测试失败（pin 为空）。

- [ ] **Step 3: src/config.cpp 实现**

文件顶部（host_name 之前）加：

```cpp
#include <random>

static std::string gen_pin() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> d(0, 999999);
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%06d", d(gen));
  return buf;
}
```

`Config::load` 中默认值区（`c.name = host_name();` 之后）加：

```cpp
  c.pin = gen_pin();
```

try 块字段恢复区加：

```cpp
        c.pin = j.value("pin", c.pin);
```

`Config::save` 的 JSON 加键：

```cpp
  nlohmann::json j{{"id", id},
                   {"name", name},
                   {"port", port},
                   {"pin", pin},
                   {"download_dir", download_dir.string()}};
```

需要 `#include <cstdio>`（snprintf）。

- [ ] **Step 4: 构建+测试全过**
- [ ] **Step 5: Commit** `git commit -am "feat: config 增加配对 PIN"`

---

### Task 3: auth 模块——AuthStore（TDD）+ History busy_timeout

**Files:**
- Create: `src/auth.h`, `src/auth.cpp`, `tests/test_auth.cpp`
- Modify: `src/history.cpp`（构造函数加一行）

- [ ] **Step 1: tests/test_auth.cpp 失败测试**

```cpp
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
```

- [ ] **Step 2: src/auth.h**

```cpp
#pragma once
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

struct PhoneInfo {
  std::string id, name, token;
  long long last_seen = 0;
};

// 配对与凭证存储：pairings（电脑互信密钥）+ phones（手机注册）
// 与 History 共用同一 db 文件（独立连接，WAL + busy_timeout）
class AuthStore {
 public:
  explicit AuthStore(const std::filesystem::path& db_file);
  ~AuthStore();
  AuthStore(const AuthStore&) = delete;
  AuthStore& operator=(const AuthStore&) = delete;

  void save_pairing(const std::string& peer_id, const std::string& secret,
                    const std::string& name);
  bool get_pairing_secret(const std::string& peer_id, std::string& out) const;
  void remove_pairing(const std::string& peer_id);

  void save_phone(const PhoneInfo& p);  // INSERT OR REPLACE
  bool find_phone_by_token(const std::string& token, PhoneInfo& out) const;
  bool find_phone_by_id(const std::string& id, PhoneInfo& out) const;
  void touch_phone(const std::string& id, long long now_ms);
  std::vector<PhoneInfo> phones() const;

 private:
  sqlite3* db_ = nullptr;
};

// PIN 防爆破：连续失败 kMaxFailures 次锁定 kLockMs；时间由调用方传入便于测试
class PinGuard {
 public:
  static constexpr int kMaxFailures = 10;
  static constexpr long long kLockMs = 300000;  // 5 分钟

  bool locked(long long now);
  void record_failure(long long now);
  void record_success();

 private:
  std::mutex mu_;
  int failures_ = 0;
  long long locked_until_ = 0;
};
```

- [ ] **Step 3: 构建确认链接失败，然后实现 src/auth.cpp**

```cpp
#include "auth.h"
#include <sqlite3.h>
#include <stdexcept>

static void exec_sql(sqlite3* db, const char* sql) {
  char* err = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    std::string e = err ? err : "sqlite 错误";
    sqlite3_free(err);
    throw std::runtime_error(e);
  }
}

AuthStore::AuthStore(const std::filesystem::path& db_file) {
  if (sqlite3_open(db_file.u8string().c_str(), &db_) != SQLITE_OK)
    throw std::runtime_error("无法打开数据库: " + db_file.string());
  exec_sql(db_, "PRAGMA journal_mode=WAL;");
  exec_sql(db_, "PRAGMA busy_timeout=3000;");
  exec_sql(db_, R"sql(
CREATE TABLE IF NOT EXISTS pairings(
  peer_id TEXT PRIMARY KEY,
  secret TEXT NOT NULL,
  name TEXT NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS phones(
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  token TEXT NOT NULL,
  last_seen INTEGER NOT NULL DEFAULT 0
);
)sql");
}

AuthStore::~AuthStore() {
  if (db_) sqlite3_close(db_);
}

void AuthStore::save_pairing(const std::string& peer_id, const std::string& secret,
                             const std::string& name) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_,
                     "INSERT OR REPLACE INTO pairings(peer_id,secret,name) VALUES(?,?,?)",
                     -1, &st, nullptr);
  sqlite3_bind_text(st, 1, peer_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, secret.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(st);
  sqlite3_finalize(st);
}

bool AuthStore::get_pairing_secret(const std::string& peer_id, std::string& out) const {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_, "SELECT secret FROM pairings WHERE peer_id=?", -1, &st, nullptr);
  sqlite3_bind_text(st, 1, peer_id.c_str(), -1, SQLITE_TRANSIENT);
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW) {
    const unsigned char* t = sqlite3_column_text(st, 0);
    out = t ? (const char*)t : "";
    found = true;
  }
  sqlite3_finalize(st);
  return found;
}

void AuthStore::remove_pairing(const std::string& peer_id) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_, "DELETE FROM pairings WHERE peer_id=?", -1, &st, nullptr);
  sqlite3_bind_text(st, 1, peer_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(st);
  sqlite3_finalize(st);
}

static PhoneInfo row_to_phone(sqlite3_stmt* st) {
  auto txt = [&](int col) {
    const unsigned char* t = sqlite3_column_text(st, col);
    return t ? std::string((const char*)t) : std::string();
  };
  PhoneInfo p;
  p.id = txt(0);
  p.name = txt(1);
  p.token = txt(2);
  p.last_seen = sqlite3_column_int64(st, 3);
  return p;
}

void AuthStore::save_phone(const PhoneInfo& p) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_,
                     "INSERT OR REPLACE INTO phones(id,name,token,last_seen) VALUES(?,?,?,?)",
                     -1, &st, nullptr);
  sqlite3_bind_text(st, 1, p.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, p.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, p.token.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 4, p.last_seen);
  sqlite3_step(st);
  sqlite3_finalize(st);
}

static bool find_phone(sqlite3* db, const char* sql, const std::string& key, PhoneInfo& out) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db, sql, -1, &st, nullptr);
  sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW) {
    out = row_to_phone(st);
    found = true;
  }
  sqlite3_finalize(st);
  return found;
}

bool AuthStore::find_phone_by_token(const std::string& token, PhoneInfo& out) const {
  return find_phone(db_, "SELECT id,name,token,last_seen FROM phones WHERE token=?", token, out);
}

bool AuthStore::find_phone_by_id(const std::string& id, PhoneInfo& out) const {
  return find_phone(db_, "SELECT id,name,token,last_seen FROM phones WHERE id=?", id, out);
}

void AuthStore::touch_phone(const std::string& id, long long now_ms) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_, "UPDATE phones SET last_seen=? WHERE id=?", -1, &st, nullptr);
  sqlite3_bind_int64(st, 1, now_ms);
  sqlite3_bind_text(st, 2, id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(st);
  sqlite3_finalize(st);
}

std::vector<PhoneInfo> AuthStore::phones() const {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_, "SELECT id,name,token,last_seen FROM phones ORDER BY name", -1, &st,
                     nullptr);
  std::vector<PhoneInfo> v;
  while (sqlite3_step(st) == SQLITE_ROW) v.push_back(row_to_phone(st));
  sqlite3_finalize(st);
  return v;
}

bool PinGuard::locked(long long now) {
  std::lock_guard<std::mutex> lk(mu_);
  return now < locked_until_;
}

void PinGuard::record_failure(long long now) {
  std::lock_guard<std::mutex> lk(mu_);
  if (++failures_ >= kMaxFailures) {
    locked_until_ = now + kLockMs;
    failures_ = 0;
  }
}

void PinGuard::record_success() {
  std::lock_guard<std::mutex> lk(mu_);
  failures_ = 0;
}
```

- [ ] **Step 4: src/history.cpp 构造函数加 busy_timeout**

在 `exec_sql(db_, "PRAGMA journal_mode=WAL;");` 之后加：

```cpp
  exec_sql(db_, "PRAGMA busy_timeout=3000;");  // 同一 db 文件现在有 History/AuthStore 两个连接
```

- [ ] **Step 5: 构建+测试全过**
- [ ] **Step 6: Commit** `git commit -am "feat: auth 模块——配对密钥与手机凭证存储"`

---

### Task 4: PinGuard 防爆破（TDD）

**Files:**
- Modify: `tests/test_auth.cpp`（末尾追加；PinGuard 类已在任务 3 的 auth.h/.cpp 中实现）

注意：PinGuard 的声明与实现已随任务 3 落地，本任务专门补行为测试（锁定/解锁/清零），若测试暴露实现 bug 则修复。

- [ ] **Step 1: tests/test_auth.cpp 末尾追加测试**

```cpp
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
```

- [ ] **Step 2: 构建+测试**，全过则直接提交；若失败修复 auth.cpp 后再过
- [ ] **Step 3: Commit** `git commit -am "test: PinGuard 防爆破行为测试"`

---

### Task 5: 配对握手与 /peer/* 鉴权

**Files:**
- Modify: `src/server.h`（成员与方法声明）
- Create: `src/server_auth.cpp`（setup_auth_routes 与辅助方法）
- Modify: `src/server.cpp`（成员初始化、调用 setup_auth_routes、/peer/text 与 /peer/file 校验、send_text 与 start_file_send 带 token）

- [ ] **Step 1: src/server.h 修改**

include 区加 `#include "auth.h"`。private 区 `setup_routes();` 之后加：

```cpp
  void setup_auth_routes();  // 配对与手机路由（定义在 server_auth.cpp）
  static bool is_local(const httplib::Request& req);  // 来源是否 127.0.0.1/::1
  bool phone_from_request(const httplib::Request& req, PhoneInfo& out) const;
  bool check_pair_token(const std::string& from_id, const httplib::Request& req) const;
```

成员 `History history_;` 之后加：

```cpp
  AuthStore auth_;
  PinGuard pin_guard_;
```

- [ ] **Step 2: src/server.cpp 构造函数初始化列表**

```cpp
App::App(Config cfg, std::filesystem::path web_dir)
    : cfg_(std::move(cfg)),
      web_dir_(std::move(web_dir)),
      history_(cfg_.db_path()),
      auth_(cfg_.db_path()) {}
```

`run()` 的启动打印改为同时显示 PIN：

```cpp
  std::printf("已启动：http://localhost:%d  （设备名：%s，配对 PIN：%s）\n", port_,
              cfg_.name.c_str(), cfg_.pin.c_str());
```

`setup_routes()` 函数体最后（/api/retry 路由之后、闭括号前）加：

```cpp
  setup_auth_routes();
```

- [ ] **Step 3: 新建 src/server_auth.cpp（本任务先放公共辅助 + /peer/pair + /api/pair）**

```cpp
#include <chrono>
#include <thread>
#include "json.hpp"
#include "server.h"
#include "util.h"

using nlohmann::json;

bool App::is_local(const httplib::Request& req) {
  return req.remote_addr == "127.0.0.1" || req.remote_addr == "::1";
}

bool App::phone_from_request(const httplib::Request& req, PhoneInfo& out) const {
  std::string token = util::get_cookie_value(req.get_header_value("Cookie"), "qt_token");
  return !token.empty() && auth_.find_phone_by_token(token, out);
}

bool App::check_pair_token(const std::string& from_id, const httplib::Request& req) const {
  std::string secret;
  if (!auth_.get_pairing_secret(from_id, secret)) return false;
  return !secret.empty() && req.get_header_value("X-Pair-Token") == secret;
}

void App::setup_auth_routes() {
  // 其他电脑 → 配对请求（凭本机 PIN 换共享密钥）
  svr_.Post("/peer/pair", [this](const httplib::Request& req, httplib::Response& res) {
    long long now = util::now_ms();
    if (pin_guard_.locked(now)) { res.status = 429; return; }
    auto j = json::parse(req.body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { res.status = 400; return; }
    std::string from_id, from_name, pin;
    try {
      from_id = j.value("from_id", "");
      from_name = j.value("from_name", "");
      pin = j.value("pin", "");
    } catch (const nlohmann::json::exception&) { res.status = 400; return; }
    if (from_id.empty()) { res.status = 400; return; }
    if (pin != cfg_.pin) {
      pin_guard_.record_failure(now);
      std::this_thread::sleep_for(std::chrono::seconds(1));  // 防爆破延迟
      res.status = 403;
      return;
    }
    pin_guard_.record_success();
    std::string secret = util::gen_secret();
    auth_.save_pairing(from_id, secret, from_name);
    bus_.publish(json{{"type", "peers"}}.dump());
    json out{{"secret", secret}, {"id", cfg_.id}, {"name", cfg_.name}};
    res.set_content(out.dump(), "application/json");
  });

  // 本机浏览器 → 向目标设备发起配对（仅 localhost）
  svr_.Post("/api/pair", [this](const httplib::Request& req, httplib::Response& res) {
    if (!is_local(req)) { res.status = 403; return; }
    auto j = json::parse(req.body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { res.status = 400; return; }
    std::string peer_id, pin;
    try {
      peer_id = j.value("peer_id", "");
      pin = j.value("pin", "");
    } catch (const nlohmann::json::exception&) { res.status = 400; return; }
    PeerInfo peer;
    if (pin.empty() || !discovery_->find(peer_id, peer)) { res.status = 400; return; }
    httplib::Client cli(peer.ip, peer.port);
    cli.set_connection_timeout(3);
    cli.set_read_timeout(5);  // 对方错误 PIN 会延迟 1 秒响应
    cli.set_write_timeout(3);
    json body{{"from_id", cfg_.id}, {"from_name", cfg_.name}, {"pin", pin}};
    auto r = cli.Post("/peer/pair", body.dump(), "application/json");
    if (!r) { res.status = 502; return; }
    if (r->status != 200) { res.status = r->status; return; }  // 403=PIN错 429=锁定
    auto rj = json::parse(r->body, nullptr, false);
    std::string secret;
    if (!rj.is_discarded() && rj.is_object()) {
      try { secret = rj.value("secret", ""); } catch (const nlohmann::json::exception&) {}
    }
    if (secret.empty()) { res.status = 502; return; }
    auth_.save_pairing(peer_id, secret, peer.name);
    bus_.publish(json{{"type", "peers"}}.dump());
    res.set_content("{}", "application/json");
  });
}
```

- [ ] **Step 4: src/server.cpp 给 /peer/text、/peer/file 加校验**

`/peer/text` handler 中，`if (m.peer_id.empty() || m.body.empty()) { res.status = 400; return; }` 之后加：

```cpp
    if (!check_pair_token(m.peer_id, req)) { res.status = 403; return; }
```

`/peer/file` handler 中，`if (name.empty() || from_id.empty()) { res.status = 400; return; }` 之后加：

```cpp
    if (!check_pair_token(from_id, req)) { res.status = 403; return; }
```

- [ ] **Step 5: src/server.cpp 发送方带 token 并处理 403**

`send_text` 整体替换为：

```cpp
void App::send_text(const PeerInfo& peer, const Message& m) {
  std::string secret;
  if (!auth_.get_pairing_secret(peer.id, secret)) {
    history_.set_status(m.id, "fail");  // 未配对
    return;
  }
  httplib::Client cli(peer.ip, peer.port);
  cli.set_connection_timeout(3);
  cli.set_read_timeout(3);
  cli.set_write_timeout(3);
  json j{{"from_id", cfg_.id}, {"from_name", cfg_.name}, {"text", m.body}};
  auto r = cli.Post("/peer/text", httplib::Headers{{"X-Pair-Token", secret}}, j.dump(),
                    "application/json");
  if (r && r->status == 403) {
    auth_.remove_pairing(peer.id);  // 对方已不认这把密钥
    bus_.publish(json{{"type", "peers"}}.dump());
  }
  history_.set_status(m.id, (r && r->status == 200) ? "ok" : "fail");
}
```

`start_file_send` 中，`if (m.id != 0 && discovery_->find(...))` 的条件块内：开头取密钥，没有则不发——把

```cpp
    if (m.id != 0 && discovery_->find(m.peer_id, peer) && fs::exists(m.file_path)) {
```

改为：

```cpp
    std::string secret;
    if (m.id != 0 && discovery_->find(m.peer_id, peer) && fs::exists(m.file_path) &&
        auth_.get_pairing_secret(m.peer_id, secret)) {
```

`cli.Post(path, httplib::Headers{}, ...)` 改为：

```cpp
      auto r = cli.Post(path, httplib::Headers{{"X-Pair-Token", secret}}, (size_t)total,
```

`ok = r && r->status == 200;` 之前加：

```cpp
      if (r && r->status == 403) {
        auth_.remove_pairing(m.peer_id);
        bus_.publish(json{{"type", "peers"}}.dump());
      }
```

- [ ] **Step 6: 构建 + 双实例集成验证**

```bash
cmake -B build && cmake --build build -j && ./build/unit_tests
rm -rf /tmp/dd_a /tmp/dd_b
./build/duoduan --data-dir /tmp/dd_a --port 8520 --name 甲 &
./build/duoduan --data-dir /tmp/dd_b --port 8530 --name 乙 &
sleep 4
B_ID=$(curl -s http://localhost:8530/api/self | python3 -c 'import sys,json;print(json.load(sys.stdin)["id"])')
B_PIN=$(python3 -c 'import json;print(json.load(open("/tmp/dd_b/config.json"))["pin"])')
# 1) 未配对发文字 → 消息 fail
curl -s -X POST http://localhost:8520/api/send-text -H 'Content-Type: application/json' \
  -d "{\"peer_id\":\"$B_ID\",\"text\":\"未配对\"}" | grep -o '"status":"fail"'
# 2) 直接伪造 /peer/text（无 token）→ 403
curl -s -X POST http://localhost:8530/peer/text -H 'Content-Type: application/json' \
  -d '{"from_id":"fake","text":"hi"}' -o /dev/null -w '%{http_code}\n'
# 3) 错误 PIN 配对 → 403（注意约 1 秒延迟）
curl -s -X POST http://localhost:8520/api/pair -H 'Content-Type: application/json' \
  -d "{\"peer_id\":\"$B_ID\",\"pin\":\"000000\"}" -o /dev/null -w '%{http_code}\n'
# 4) 正确 PIN 配对 → 200
curl -s -X POST http://localhost:8520/api/pair -H 'Content-Type: application/json' \
  -d "{\"peer_id\":\"$B_ID\",\"pin\":\"$B_PIN\"}" -o /dev/null -w '%{http_code}\n'
# 5) 配对后发文字 → ok，且乙收到
curl -s -X POST http://localhost:8520/api/send-text -H 'Content-Type: application/json' \
  -d "{\"peer_id\":\"$B_ID\",\"text\":\"配对后\"}" | grep -o '"status":"ok"'
# 6) 反向（乙→甲）无需再配对（双向互信）
A_ID=$(curl -s http://localhost:8520/api/self | python3 -c 'import sys,json;print(json.load(sys.stdin)["id"])')
curl -s -X POST http://localhost:8530/api/send-text -H 'Content-Type: application/json' \
  -d "{\"peer_id\":\"$A_ID\",\"text\":\"反向\"}" | grep -o '"status":"ok"'
pkill -f 'build/duoduan'; rm -rf /tmp/dd_a /tmp/dd_b
```

预期依次：`"status":"fail"`、`403`、`403`、`200`、`"status":"ok"`、`"status":"ok"`

- [ ] **Step 7: Commit** `git commit -am "feat: 电脑间 PIN 配对与 /peer/* 强制鉴权"`

---

### Task 6: 鉴权边界（pre-routing）与手机注册

**Files:**
- Modify: `src/server.cpp`（setup_routes 开头加 pre-routing；/api/self 加字段）
- Modify: `src/server_auth.cpp`（setup_auth_routes 追加 /api/phone/register）

- [ ] **Step 1: src/server.cpp 的 setup_routes() 开头（set_mount_point 之前）加 pre-routing**

```cpp
  // 鉴权边界：localhost 全放行；静态文件放行；远程 /api/* 走白名单+token
  svr_.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
    if (is_local(req)) return httplib::Server::HandlerResponse::Unhandled;
    const std::string& p = req.path;
    if (p.rfind("/api/", 0) != 0)
      return httplib::Server::HandlerResponse::Unhandled;  // 静态文件与 /peer/*（各自校验）
    if (p == "/api/phone/register")
      return httplib::Server::HandlerResponse::Unhandled;  // 注册入口（内部验 PIN）
    static const char* kRemoteAllowed[] = {
        "/api/self",          "/api/events",          "/api/messages",
        "/api/file",          "/api/phone/send-text", "/api/phone/send-file",
        "/api/phone/heartbeat"};
    bool allowed = false;
    for (const char* a : kRemoteAllowed)
      if (p == a) { allowed = true; break; }
    if (!allowed) {  // 远程不允许的接口（send-text/pair/retry 等）即使有 token 也拒绝
      res.status = 403;
      res.set_content("{\"error\":\"forbidden\"}", "application/json");
      return httplib::Server::HandlerResponse::Handled;
    }
    PhoneInfo ph;
    if (phone_from_request(req, ph))
      return httplib::Server::HandlerResponse::Unhandled;
    res.status = 401;
    res.set_content("{\"error\":\"unauthorized\"}", "application/json");
    return httplib::Server::HandlerResponse::Handled;
  });
```

- [ ] **Step 2: /api/self 加 is_remote 与 pin（仅本机）**

handler 整体替换为：

```cpp
  svr_.Get("/api/self", [this](const httplib::Request& req, httplib::Response& res) {
    json j{{"id", cfg_.id}, {"name", cfg_.name}, {"port", port_},
           {"is_remote", !is_local(req)}};
    if (is_local(req)) j["pin"] = cfg_.pin;
    res.set_content(j.dump(), "application/json");
  });
```

- [ ] **Step 3: src/server_auth.cpp 的 setup_auth_routes() 末尾追加注册路由**

```cpp
  // 手机首次注册：设备名 + PIN → 长期 Cookie
  svr_.Post("/api/phone/register", [this](const httplib::Request& req, httplib::Response& res) {
    long long now = util::now_ms();
    if (pin_guard_.locked(now)) { res.status = 429; return; }
    auto j = json::parse(req.body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { res.status = 400; return; }
    std::string phone_id, name, pin;
    try {
      phone_id = j.value("phone_id", "");
      name = j.value("name", "");
      pin = j.value("pin", "");
    } catch (const nlohmann::json::exception&) { res.status = 400; return; }
    if (phone_id.size() < 8 || phone_id.size() > 64 || name.empty()) {
      res.status = 400;
      return;
    }
    if (pin != cfg_.pin) {
      pin_guard_.record_failure(now);
      std::this_thread::sleep_for(std::chrono::seconds(1));
      res.status = 403;
      return;
    }
    pin_guard_.record_success();
    PhoneInfo p;
    p.id = phone_id;
    p.name = name;
    p.token = util::gen_secret();
    p.last_seen = now;
    auth_.save_phone(p);
    res.set_header("Set-Cookie", "qt_token=" + p.token +
                                     "; Max-Age=31536000; Path=/; HttpOnly; SameSite=Lax");
    bus_.publish(json{{"type", "peers"}}.dump());
    res.set_content("{}", "application/json");
  });
```

- [ ] **Step 4: 构建 + 集成验证（用本机局域网 IP 模拟"远程"）**

```bash
cmake -B build && cmake --build build -j && ./build/unit_tests
rm -rf /tmp/dd_a
./build/duoduan --data-dir /tmp/dd_a --port 8520 --name 甲 &
sleep 1
LAN_IP=$(hostname -I | awk '{print $1}')
A_PIN=$(python3 -c 'import json;print(json.load(open("/tmp/dd_a/config.json"))["pin"])')
# 1) localhost 的 self 有 pin、is_remote=false
curl -s http://localhost:8520/api/self | grep -o '"is_remote":false'
curl -s http://localhost:8520/api/self | grep -c '"pin"'
# 2) 远程无 token：self → 401；静态页 → 200
curl -s http://$LAN_IP:8520/api/self -o /dev/null -w '%{http_code}\n'
curl -s http://$LAN_IP:8520/ -o /dev/null -w '%{http_code}\n'
# 3) 远程访问白名单外接口（即使之后有 token 也该 403）
curl -s -X POST http://$LAN_IP:8520/api/send-text -o /dev/null -w '%{http_code}\n'
# 4) 注册：错 PIN → 403；对 PIN → 200 + Set-Cookie
curl -s -X POST http://$LAN_IP:8520/api/phone/register -H 'Content-Type: application/json' \
  -d '{"phone_id":"testphone01","name":"iPhone","pin":"000000"}' -o /dev/null -w '%{http_code}\n'
curl -si -X POST http://$LAN_IP:8520/api/phone/register -H 'Content-Type: application/json' \
  -d "{\"phone_id\":\"testphone01\",\"name\":\"iPhone\",\"pin\":\"$A_PIN\"}" | grep -i 'set-cookie' 
# 5) 带 cookie 后 self → 200 且 is_remote=true 且无 pin 字段
TOKEN=$(curl -si -X POST http://$LAN_IP:8520/api/phone/register -H 'Content-Type: application/json' \
  -d "{\"phone_id\":\"testphone01\",\"name\":\"iPhone\",\"pin\":\"$A_PIN\"}" \
  | grep -i set-cookie | sed 's/.*qt_token=\([a-f0-9]*\).*/\1/')
curl -s -b "qt_token=$TOKEN" http://$LAN_IP:8520/api/self
echo
pkill -f 'build/duoduan'; rm -rf /tmp/dd_a
```

预期：`is_remote:false` / pin 计数 1 / `401` / `200` / `403` / `403`、Set-Cookie 行 / 最后输出含 `"is_remote":true` 且不含 `"pin"`。

- [ ] **Step 5: Commit** `git commit -am "feat: 鉴权边界与手机 PIN 注册"`

---

### Task 7: 手机会话 API 与电脑→手机分支

**Files:**
- Modify: `src/server_auth.cpp`（追加 send-text/send-file/heartbeat 路由）
- Modify: `src/server.cpp`（/api/messages 越权限制、/api/peers 合并手机、/api/send-text 与 /api/send-file 的手机分支）

- [ ] **Step 1: src/server_auth.cpp 的 setup_auth_routes() 末尾追加三个路由**

```cpp
  // 手机 → 发文字（写入本机即完成）
  svr_.Post("/api/phone/send-text", [this](const httplib::Request& req, httplib::Response& res) {
    PhoneInfo ph;
    if (!phone_from_request(req, ph)) { res.status = 401; return; }
    auto j = json::parse(req.body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { res.status = 400; return; }
    std::string text;
    try { text = j.value("text", ""); } catch (const nlohmann::json::exception&) {
      res.status = 400;
      return;
    }
    if (text.empty()) { res.status = 400; return; }
    Message m;
    m.peer_id = ph.id;
    m.direction = "in";  // 电脑视角：收到
    m.kind = "text";
    m.body = text;
    m.status = "ok";
    history_.add(m);
    auth_.touch_phone(ph.id, util::now_ms());
    publish_message("message", m);
    res.set_content(to_json(m).dump(), "application/json");
  });

  // 手机 → 上传文件（直接存下载目录）
  svr_.Post("/api/phone/send-file",
            [this](const httplib::Request& req, httplib::Response& res,
                   const httplib::ContentReader& reader) {
    namespace fs = std::filesystem;
    PhoneInfo ph;
    if (!phone_from_request(req, ph)) { res.status = 401; return; }
    fs::create_directories(cfg_.download_dir);
    std::string filename;
    fs::path target;
    auto ofs = std::make_shared<std::ofstream>();
    bool skipping = false;  // 只收第一个 file part（与 /api/send-file 同惯例）
    reader(
        [&](const httplib::MultipartFormData& file) {
          if (ofs->is_open() || !filename.empty()) {
            skipping = true;
            return true;
          }
          filename = fs::path(file.filename).filename().string();
          if (filename.empty()) filename = "未命名";
          target = util::unique_path(cfg_.download_dir, filename);
          ofs->open(target, std::ios::binary);
          return ofs->good();
        },
        [&](const char* data, size_t len) {
          if (skipping) return true;
          ofs->write(data, (std::streamsize)len);
          return ofs->good();
        });
    if (ofs->is_open()) ofs->close();
    std::error_code ec;
    if (filename.empty() || !fs::exists(target)) {
      if (!target.empty()) fs::remove(target, ec);
      res.status = 400;
      return;
    }
    Message m;
    m.peer_id = ph.id;
    m.direction = "in";
    m.kind = "file";
    m.file_name = target.filename().string();
    m.file_size = (long long)fs::file_size(target, ec);
    m.file_path = target.string();
    m.status = "ok";
    history_.add(m);
    auth_.touch_phone(ph.id, util::now_ms());
    publish_message("message", m);
    res.set_content(to_json(m).dump(), "application/json");
  });

  // 手机心跳（在线状态）
  svr_.Post("/api/phone/heartbeat", [this](const httplib::Request& req, httplib::Response& res) {
    PhoneInfo ph;
    if (!phone_from_request(req, ph)) { res.status = 401; return; }
    auth_.touch_phone(ph.id, util::now_ms());
    res.set_content("{}", "application/json");
  });
```

需要在 server_auth.cpp 顶部补 `#include <fstream>`。

- [ ] **Step 1b: src/server.cpp 的 /api/events 加手机过滤（越权防护：手机只收自己会话的事件）**

handler 整体替换为：

```cpp
  svr_.Get("/api/events", [this](const httplib::Request& req, httplib::Response& res) {
    std::string phone_filter;  // 非空：只推送该手机会话的消息事件
    if (!is_local(req)) {
      PhoneInfo ph;
      if (!phone_from_request(req, ph)) { res.status = 401; return; }
      phone_filter = ph.id;
    }
    auto sub = bus_.subscribe();
    res.set_header("Cache-Control", "no-cache");
    res.set_chunked_content_provider(
        "text/event-stream",
        [sub, phone_filter](size_t, httplib::DataSink& sink) {
          std::string chunk;
          {
            std::unique_lock<std::mutex> lk(sub->mu);
            sub->cv.wait_for(lk, std::chrono::seconds(5),
                             [&] { return !sub->queue.empty(); });
            if (sub->queue.empty()) {
              chunk = ": ping\n\n";  // 保活注释行
            } else {
              while (!sub->queue.empty()) {
                const std::string& payload = sub->queue.front();
                bool pass = true;
                if (!phone_filter.empty()) {
                  auto j = json::parse(payload, nullptr, false);
                  pass = !j.is_discarded() && j.is_object() &&
                         j.contains("message") && j["message"].is_object() &&
                         j["message"].value("peer_id", "") == phone_filter;
                }
                if (pass) chunk += "data: " + payload + "\n\n";
                sub->queue.pop_front();
              }
              if (chunk.empty()) chunk = ": skip\n\n";  // 全被过滤也写一行保活
            }
          }
          return sink.write(chunk.data(), chunk.size());
        },
        [this, sub](bool) { bus_.unsubscribe(sub); });
  });
```

- [ ] **Step 2: src/server.cpp 的 /api/messages 加手机越权限制**

handler 整体替换为：

```cpp
  svr_.Get("/api/messages", [this](const httplib::Request& req, httplib::Response& res) {
    auto peer = req.get_param_value("peer");
    if (!is_local(req)) {  // 手机只能看自己的会话
      PhoneInfo ph;
      if (!phone_from_request(req, ph)) { res.status = 401; return; }
      peer = ph.id;
    }
    json arr = json::array();
    for (auto& m : history_.list(peer)) arr.push_back(to_json(m));
    res.set_content(arr.dump(), "application/json");
  });
```

- [ ] **Step 3: src/server.cpp 的 /api/peers 合并手机与 paired 标记**

handler 整体替换为：

```cpp
  svr_.Get("/api/peers", [this](const httplib::Request&, httplib::Response& res) {
    json arr = json::array();
    long long now = util::now_ms();
    for (auto& p : discovery_->peers()) {
      std::string secret;
      arr.push_back({{"id", p.id},
                     {"name", p.name},
                     {"ip", p.ip},
                     {"port", p.port},
                     {"type", "pc"},
                     {"paired", auth_.get_pairing_secret(p.id, secret)},
                     {"online", now - p.last_seen < discovery::kOfflineMs}});
    }
    for (auto& ph : auth_.phones()) {
      arr.push_back({{"id", ph.id},
                     {"name", ph.name},
                     {"type", "phone"},
                     {"paired", true},
                     {"online", now - ph.last_seen < 30000}});
    }
    res.set_content(arr.dump(), "application/json");
  });
```

- [ ] **Step 4: src/server.cpp 的 /api/send-text 加手机分支**

`PeerInfo peer;` 与 `if (text.empty() || !discovery_->find(peer_id, peer))` 之间插入：

```cpp
    PhoneInfo ph;
    if (!text.empty() && auth_.find_phone_by_id(peer_id, ph)) {
      // 发给手机：本地写入即完成，无网络推送
      Message m;
      m.peer_id = peer_id;
      m.direction = "out";
      m.kind = "text";
      m.body = text;
      m.status = "ok";
      history_.add(m);
      publish_message("message", m);
      res.set_content(to_json(m).dump(), "application/json");
      return;
    }
```

- [ ] **Step 5: src/server.cpp 的 /api/send-file 加手机分支**

在 `Message m;` 构建段之前（`if (filename.empty() || !fs::exists(staged))` 检查之后）插入；同时把开头的 peer 校验改为兼容手机。把 handler 开头：

```cpp
    std::string peer_id = req.get_param_value("peer");
    PeerInfo peer;
    if (!discovery_->find(peer_id, peer)) { res.status = 400; return; }
```

改为：

```cpp
    std::string peer_id = req.get_param_value("peer");
    PeerInfo peer;
    PhoneInfo ph;
    bool to_phone = auth_.find_phone_by_id(peer_id, ph);
    if (!to_phone && !discovery_->find(peer_id, peer)) { res.status = 400; return; }
```

把消息构建与发送段：

```cpp
    m.status = "pending";
    history_.add(m);
    publish_message("message", m);
    start_file_send(m.id);
```

改为：

```cpp
    m.status = to_phone ? "ok" : "pending";  // 手机：文件留在暂存区供其下载，立即完成
    history_.add(m);
    publish_message("message", m);
    if (!to_phone) start_file_send(m.id);
```

- [ ] **Step 6: 构建 + 集成验证（注册→手机收发→电脑回发）**

```bash
cmake -B build && cmake --build build -j && ./build/unit_tests
rm -rf /tmp/dd_a /tmp/dd_a_dl
./build/duoduan --data-dir /tmp/dd_a --port 8520 --name 甲 --download-dir /tmp/dd_a_dl &
sleep 1
LAN_IP=$(hostname -I | awk '{print $1}')
A_PIN=$(python3 -c 'import json;print(json.load(open("/tmp/dd_a/config.json"))["pin"])')
TOKEN=$(curl -si -X POST http://$LAN_IP:8520/api/phone/register -H 'Content-Type: application/json' \
  -d "{\"phone_id\":\"testphone01\",\"name\":\"iPhone\",\"pin\":\"$A_PIN\"}" \
  | grep -i set-cookie | sed 's/.*qt_token=\([a-f0-9]*\).*/\1/')
# 1) 手机发文字 → 电脑历史 direction=in
curl -s -b "qt_token=$TOKEN" -X POST http://$LAN_IP:8520/api/phone/send-text \
  -H 'Content-Type: application/json' -d '{"text":"来自手机"}' | grep -o '"direction":"in"'
# 2) 手机传文件 → 落到下载目录
echo "手机文件内容" > /tmp/ph.txt
curl -s -b "qt_token=$TOKEN" -X POST http://$LAN_IP:8520/api/phone/send-file -F "file=@/tmp/ph.txt" \
  | grep -o '"kind":"file"'
cat /tmp/dd_a_dl/ph.txt
# 3) 手机心跳 + peers 出现手机（online true, type phone）
curl -s -b "qt_token=$TOKEN" -X POST http://$LAN_IP:8520/api/phone/heartbeat > /dev/null
curl -s http://localhost:8520/api/peers | python3 -m json.tool | grep -E 'phone|iPhone|online'
# 4) 电脑发文字给手机 → status ok
curl -s -X POST http://localhost:8520/api/send-text -H 'Content-Type: application/json' \
  -d '{"peer_id":"testphone01","text":"回手机"}' | grep -o '"status":"ok"'
# 5) 电脑发文件给手机 → status ok 且暂存保留
echo "给手机的文件" > /tmp/pc.txt
curl -s -X POST "http://localhost:8520/api/send-file?peer=testphone01" -F "file=@/tmp/pc.txt" \
  | grep -o '"status":"ok"'
ls /tmp/dd_a/staging/
# 6) 手机拉消息（无 peer 参数也只看到自己的会话）
curl -s -b "qt_token=$TOKEN" "http://$LAN_IP:8520/api/messages" | python3 -m json.tool | grep -c '"peer_id": "testphone01"'
pkill -f 'build/duoduan'; rm -rf /tmp/dd_a /tmp/dd_a_dl /tmp/ph.txt /tmp/pc.txt
```

预期：`"direction":"in"` / `"kind":"file"` + 文件内容一致 / peers 含 phone 且 online true / `"status":"ok"` ×2 / staging 有 pc.txt / 消息计数 ≥4。

- [ ] **Step 7: Commit** `git commit -am "feat: 手机会话收发与电脑→手机分支"`

---

### Task 8: /api/file 文件下载（流式 + 越权防护）

**Files:**
- Modify: `src/server_auth.cpp`（setup_auth_routes 末尾追加）

- [ ] **Step 1: 追加下载路由**

```cpp
  // 下载消息附件：localhost 任意；手机仅限自己会话的消息
  svr_.Get("/api/file", [this](const httplib::Request& req, httplib::Response& res) {
    namespace fs = std::filesystem;
    long long id = std::atoll(req.get_param_value("id").c_str());
    Message m = history_.get(id);
    if (m.id == 0 || m.kind != "file" || m.file_path.empty()) { res.status = 404; return; }
    if (!is_local(req)) {
      PhoneInfo ph;
      if (!phone_from_request(req, ph) || m.peer_id != ph.id) { res.status = 403; return; }
    }
    auto file = std::make_shared<std::ifstream>(fs::path(m.file_path), std::ios::binary);
    std::error_code ec;
    auto size = fs::file_size(fs::path(m.file_path), ec);
    if (!*file || ec) { res.status = 404; return; }
    res.set_header("Content-Disposition",
                   "attachment; filename*=UTF-8''" + util::url_encode(m.file_name));
    res.set_content_provider(
        (size_t)size, "application/octet-stream",
        [file](size_t offset, size_t length, httplib::DataSink& sink) {
          file->seekg((std::streamoff)offset);
          char buf[65536];
          size_t want = std::min(length, sizeof(buf));
          file->read(buf, (std::streamsize)want);
          std::streamsize n = file->gcount();
          if (n <= 0) return false;
          return sink.write(buf, (size_t)n);
        });
  });
```

需要 `#include <cstdlib>`（atoll）与 `#include <algorithm>`（std::min）——在 server_auth.cpp 顶部确认补齐。

- [ ] **Step 2: 构建 + 集成验证**

```bash
cmake -B build && cmake --build build -j && ./build/unit_tests
rm -rf /tmp/dd_a /tmp/dd_a_dl
./build/duoduan --data-dir /tmp/dd_a --port 8520 --name 甲 --download-dir /tmp/dd_a_dl &
sleep 1
LAN_IP=$(hostname -I | awk '{print $1}')
A_PIN=$(python3 -c 'import json;print(json.load(open("/tmp/dd_a/config.json"))["pin"])')
TOKEN=$(curl -si -X POST http://$LAN_IP:8520/api/phone/register -H 'Content-Type: application/json' \
  -d "{\"phone_id\":\"testphone01\",\"name\":\"iPhone\",\"pin\":\"$A_PIN\"}" \
  | grep -i set-cookie | sed 's/.*qt_token=\([a-f0-9]*\).*/\1/')
TOKEN2=$(curl -si -X POST http://$LAN_IP:8520/api/phone/register -H 'Content-Type: application/json' \
  -d "{\"phone_id\":\"otherphone1\",\"name\":\"安卓\",\"pin\":\"$A_PIN\"}" \
  | grep -i set-cookie | sed 's/.*qt_token=\([a-f0-9]*\).*/\1/')
# 电脑发一个文件给 testphone01
head -c 300000 /dev/urandom > /tmp/dl.bin
MSG_ID=$(curl -s -X POST "http://localhost:8520/api/send-file?peer=testphone01" -F "file=@/tmp/dl.bin" \
  | python3 -c 'import sys,json;print(json.load(sys.stdin)["id"])')
# 1) 手机下载 → 内容一致 + 中文 Content-Disposition 头
curl -s -b "qt_token=$TOKEN" "http://$LAN_IP:8520/api/file?id=$MSG_ID" -o /tmp/dl_got.bin
cmp /tmp/dl.bin /tmp/dl_got.bin && echo "下载一致"
curl -sI -b "qt_token=$TOKEN" "http://$LAN_IP:8520/api/file?id=$MSG_ID" | grep -i content-disposition
# 2) 另一部手机下载这条消息 → 403（越权）
curl -s -b "qt_token=$TOKEN2" "http://$LAN_IP:8520/api/file?id=$MSG_ID" -o /dev/null -w '%{http_code}\n'
# 3) 无 token → 401（pre-routing 拦截）
curl -s "http://$LAN_IP:8520/api/file?id=$MSG_ID" -o /dev/null -w '%{http_code}\n'
# 4) localhost 不受限
curl -s "http://localhost:8520/api/file?id=$MSG_ID" -o /tmp/dl_local.bin && cmp /tmp/dl.bin /tmp/dl_local.bin && echo "本机下载一致"
# 5) 不存在的 id → 404
curl -s "http://localhost:8520/api/file?id=99999" -o /dev/null -w '%{http_code}\n'
pkill -f 'build/duoduan'; rm -rf /tmp/dd_a /tmp/dd_a_dl /tmp/dl.bin /tmp/dl_got.bin /tmp/dl_local.bin
```

预期：`下载一致` + Content-Disposition 行 / `403` / `401` / `本机下载一致` / `404`

- [ ] **Step 3: Commit** `git commit -am "feat: 消息附件下载接口（流式+越权防护）"`

---

### Task 9: 前端——登录视图与手机模式

**Files:**
- Modify: `web/index.html`（body 内追加登录视图）
- Modify: `web/style.css`（末尾追加）
- Modify: `web/app.js`（改启动逻辑、加手机模式与登录逻辑、收发分支、气泡方向）

- [ ] **Step 1: web/index.html 在 `<script src="app.js"></script>` 之前加登录视图**

```html
<div id="login" hidden>
  <div id="login-box">
    <h2>连接到电脑</h2>
    <input id="login-name" placeholder="给这台设备起个名字（如 iPhone）" maxlength="20">
    <input id="login-pin" placeholder="电脑屏幕上的 6 位 PIN" inputmode="numeric" maxlength="6">
    <button id="login-btn">连接</button>
    <div id="login-err"></div>
  </div>
</div>
```

- [ ] **Step 2: web/style.css 末尾追加**

```css
#login { position: fixed; inset: 0; background: #f5f5f5; display: flex;
         align-items: center; justify-content: center; z-index: 10; }
#login-box { background: #fff; padding: 28px 24px; border-radius: 12px;
             box-shadow: 0 2px 12px rgba(0,0,0,.12); width: min(90vw, 320px);
             display: flex; flex-direction: column; gap: 12px; }
#login-box h2 { font-size: 18px; }
#login-box input { border: 1px solid #ccc; border-radius: 6px; padding: 10px; font: inherit; }
#login-box button { background: #07c160; color: #fff; border: none; border-radius: 6px;
                    padding: 11px; font: inherit; cursor: pointer; }
#login-err { color: #e53935; font-size: 13px; min-height: 1em; }

body.phone #sidebar { display: none; }
body.phone .bubble { max-width: 85%; }
.dl { display: inline-block; margin-top: 6px; font-size: 13px; color: #07c160;
      text-decoration: none; font-weight: bold; }
```

- [ ] **Step 3: web/app.js 修改**

3a. 顶部状态行改为：

```js
let self = null, peers = [], current = null, messages = [], phoneMode = false;
```

3b. 在 `fmtTime` 之后加：

```js
function phoneId() {
  let id = localStorage.getItem('qt_phone_id');
  if (!id) {
    id = [...crypto.getRandomValues(new Uint8Array(8))]
      .map((b) => b.toString(16).padStart(2, '0')).join('');
    localStorage.setItem('qt_phone_id', id);
  }
  return id;
}

function showLogin() {
  $('#login').hidden = false;
  $('#login-btn').onclick = async () => {
    const name = $('#login-name').value.trim();
    const pin = $('#login-pin').value.trim();
    if (!name || pin.length !== 6) {
      $('#login-err').textContent = '请填写设备名和 6 位 PIN';
      return;
    }
    try {
      await api('/api/phone/register', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ phone_id: phoneId(), name, pin }),
      });
      location.reload();
    } catch (e) {
      $('#login-err').textContent = String(e).includes('429')
        ? '尝试次数过多，请 5 分钟后再试' : 'PIN 错误，请核对电脑屏幕上的数字';
    }
  };
}
```

3c. `msgEl` 中方向类与下载按钮。开头：

```js
function msgEl(m) {
  const mine = phoneMode ? m.direction === 'in' : m.direction === 'out';
  const d = document.createElement('div');
  d.className = 'msg ' + (mine ? 'out' : 'in') + (m.status === 'fail' ? ' fail' : '');
```

（原 `'msg ' + m.direction + ...` 一行替换为上面两行的逻辑。）

文件气泡部分，在 `b.append(fname, fmeta);` 之后加下载按钮逻辑：

```js
    const downloadable = m.status === 'ok' && m.file_path &&
      (phoneMode ? m.direction === 'out' : true);
    if (downloadable) {
      const a = document.createElement('a');
      a.className = 'dl';
      a.href = '/api/file?id=' + m.id;
      a.textContent = '⬇ 下载';
      b.appendChild(a);
    }
```

同时把 fmeta 文案改为手机端不显示本机路径：

```js
    fmeta.textContent = fmtSize(m.file_size) +
      (!phoneMode && m.direction === 'in' && m.file_path ? ' · 已保存到 ' + m.file_path : '');
```

3d. `sendText` 与 `sendFiles` 加手机分支——`sendText` 的 fetch 部分改为：

```js
    const m = phoneMode
      ? await api('/api/phone/send-text', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ text: t }),
        })
      : await api('/api/send-text', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ peer_id: current, text: t }),
        });
```

`sendFiles` 的 fetch 部分改为：

```js
      const url = phoneMode
        ? '/api/phone/send-file'
        : '/api/send-file?peer=' + encodeURIComponent(current);
      const m = await api(url, { method: 'POST', body: fd });
```

3e. 启动块整体替换（文件末尾的立即执行函数）：

```js
(async () => {
  try {
    self = await api('/api/self');
  } catch (e) {
    if (String(e).includes('401')) { showLogin(); return; }
    throw e;
  }
  if (self.is_remote) {
    phoneMode = true;
    document.body.classList.add('phone');
    document.title = '多端互通 - ' + self.name;
    $('#chat-header').textContent = self.name;
    $('#composer').hidden = false;
    current = phoneId();
    messages = await api('/api/messages?peer=' + encodeURIComponent(current));
    renderMessages();
    setInterval(() => api('/api/phone/heartbeat', { method: 'POST' }).catch(() => {}), 10000);
  } else {
    $('#self-name').textContent = '本机：' + self.name;
    $('#self-pin').textContent = '配对 PIN：' + self.pin;
    document.title = '多端互通 - ' + self.name;
    await refreshPeers();
    setInterval(refreshPeers, 5000);
  }
})();
```

注意：`#self-pin` 元素将在任务 10 加入 index.html；本任务先在 index.html 的 `#self-name` 之后加上：

```html
  <div id="self-pin"></div>
```

并在 style.css 追加：

```css
#self-pin { padding: 8px 14px; font-size: 13px; color: #aaa; border-bottom: 1px solid #444; }
```

3f. SSE 的 `refreshPeers` 在手机模式下不需要（peers 接口对手机也不在白名单……实际在白名单外吗？/api/peers 不在远程白名单——手机调用会 403）。把 `es.onmessage` 与 `es.onopen` 改为：

```js
es.onopen = () => {
  if (current) {
    api('/api/messages?peer=' + encodeURIComponent(current))
      .then((ms) => { messages = ms; renderMessages(); })
      .catch(() => {});
  }
  if (!phoneMode) refreshPeers();
};
es.onmessage = (e) => {
  const ev = JSON.parse(e.data);
  if (ev.type === 'peers' && !phoneMode) refreshPeers();
  if (ev.type === 'message' || ev.type === 'message_update') upsertMessage(ev.message);
  if (ev.type === 'progress') {
    const el = document.querySelector(`.msg[data-id="${ev.message_id}"] .bar i`);
    if (el) el.style.width = ((ev.sent * 100) / ev.total).toFixed(1) + '%';
  }
};
```

（原 es.onopen 调 selectPeer 的版本被上面替换——手机模式没有 selectPeer。）

- [ ] **Step 4: 验证**

```bash
node --check web/app.js
cmake --build build -j >/dev/null
rm -rf /tmp/dd_a
./build/duoduan --data-dir /tmp/dd_a --port 8520 --name 甲 &
sleep 1
LAN_IP=$(hostname -I | awk '{print $1}')
curl -s http://$LAN_IP:8520/ | grep -c 'login-box'   # 登录视图已在页面里 → 1
pkill -f 'build/duoduan'; rm -rf /tmp/dd_a
```

- [ ] **Step 5: Commit** `git commit -am "feat: 前端登录视图与手机单会话模式"`

---

### Task 10: 前端——设备分组折叠、配对弹框、下载按钮回归

**Files:**
- Modify: `web/app.js`（renderPeers 分组 + pairWith）
- Modify: `web/style.css`（分组样式）

- [ ] **Step 1: web/app.js 的 renderPeers 整体替换**

```js
function renderPeers() {
  const el = $('#peer-list');
  el.innerHTML = '';
  const mkRow = (p) => {
    const d = document.createElement('div');
    d.className = 'peer' + (current === p.id ? ' active' : '') + (p.online ? '' : ' offline');
    const dot = document.createElement('span');
    dot.className = 'dot';
    const name = document.createElement('span');
    name.className = 'pname';
    name.textContent = (p.type === 'phone' ? '📱 ' : '💻 ') + p.name + (p.paired ? '' : ' 🔒');
    d.append(dot, name);
    d.onclick = () => (p.paired ? selectPeer(p.id) : pairWith(p));
    return d;
  };
  const paired = peers.filter((p) => p.paired);
  const unpaired = peers.filter((p) => !p.paired);
  for (const p of paired) el.appendChild(mkRow(p));
  if (unpaired.length) {
    if (!paired.length) {
      for (const p of unpaired) el.appendChild(mkRow(p));  // 还没配对过：平铺方便首次配对
    } else {
      const det = document.createElement('details');
      const sum = document.createElement('summary');
      sum.textContent = `未配对设备 (${unpaired.length})`;
      det.appendChild(sum);
      for (const p of unpaired) det.appendChild(mkRow(p));
      el.appendChild(det);
    }
  }
  if (!peers.length) {
    el.innerHTML = '<div class="empty">正在搜索局域网设备…<br>请确认对方已启动本程序</div>';
  }
}

async function pairWith(p) {
  const pin = prompt(`与「${p.name}」配对\n请输入对方屏幕上显示的 6 位 PIN：`);
  if (!pin) return;
  try {
    await api('/api/pair', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ peer_id: p.id, pin: pin.trim() }),
    });
    await refreshPeers();
  } catch (e) {
    alert(String(e).includes('429') ? '对方已锁定，请 5 分钟后再试'
                                    : '配对失败：PIN 错误或对方不在线');
  }
}
```

- [ ] **Step 2: web/style.css 末尾追加分组样式**

```css
#peer-list details { border-top: 1px solid #444; }
#peer-list summary { padding: 12px 14px; color: #888; font-size: 13px; cursor: pointer;
                     list-style-position: inside; }
#peer-list details .peer { padding-left: 24px; }
```

- [ ] **Step 3: 验证（语法 + 双实例配对流程冒烟）**

```bash
node --check web/app.js && echo "JS OK"
cmake --build build -j >/dev/null
rm -rf /tmp/dd_a /tmp/dd_b
./build/duoduan --data-dir /tmp/dd_a --port 8520 --name 甲 &
./build/duoduan --data-dir /tmp/dd_b --port 8530 --name 乙 &
sleep 4
curl -s http://localhost:8520/api/peers | python3 -m json.tool | grep -E 'paired|name'
pkill -f 'build/duoduan'; rm -rf /tmp/dd_a /tmp/dd_b
```

预期：乙出现且 `"paired": false`。

- [ ] **Step 4: Commit** `git commit -am "feat: 设备分组折叠与 PIN 配对交互"`

---

### Task 11: 回归与 README 更新

**Files:**
- Modify: `README.md`

- [ ] **Step 1: 全量回归**

```bash
cmake -B build && cmake --build build -j && ./build/unit_tests
```

再按任务 5 Step 6（配对链路）和任务 7 Step 6（手机链路）各跑一遍核心命令，确认通过。

- [ ] **Step 2: README.md 更新**

"运行"一节后追加：

````markdown
## 配对（v2 起必需）

出于安全考虑，设备之间必须配对一次才能互传：

- **电脑 ↔ 电脑**：在 A 的页面左侧"未配对设备"里点击 B，输入 B 屏幕上显示的
  6 位 PIN（启动时终端会打印，页面侧栏也显示）。配对一次双向有效。
- **手机接入**：手机连同一 WiFi，浏览器打开 `http://电脑IP:8520`，输入设备名和
  电脑的 PIN 即可。之后这部手机就出现在电脑的设备列表里（📱），互发文字和文件，
  电脑发的文件在手机上点"⬇ 下载"保存。

注意：手机锁屏或切走后传输会中断（浏览器限制），回到页面会自动重连并补全记录。
发给手机的文件保留在 `~/.duoduan/staging/`，手机可随时重新下载。

## 安全说明

- 局域网内未配对的设备只能"看见"你的设备名，无法发送文件或查看页面
- PIN 连续输错 10 次锁定 5 分钟
- 手机凭证仅能访问自己的会话，无法查看其他设备的聊天记录
````

并删除旧"安全说明"一节（"当前版本无身份验证"已不再成立），路线图中划掉已完成项：

```markdown
## 路线图

- [x] 手机浏览器接入（手机 ↔ 电脑会话）
- [x] 配对 PIN 码
- [ ] 取消配对/设备管理界面
```

- [ ] **Step 3: Commit** `git commit -am "docs: README 更新配对与手机使用说明"`

---

## 验收标准（对照设计文档）

- [→任务5] 电脑配对握手、共享密钥、X-Pair-Token 校验、403 失效回退（设计 §2.1）
- [→任务6] PIN 生成显示、pre-routing 边界、手机注册 Cookie、防爆破（§2.2/§2.3/§3）
- [→任务7] 手机收发、电脑→手机分支、messages 越权、peers 合并、心跳在线（§4/§5）
- [→任务8] /api/file 流式下载 + 越权防护（§4）
- [→任务9] 登录视图、手机单会话模式、气泡翻转、响应式（§6）
- [→任务10] 分组折叠（无配对时平铺）、PIN 弹框、本机 PIN 显示（§6）
- [→任务11] README、回归（§7 兼容性说明含在 README）
