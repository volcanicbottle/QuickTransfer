# 二维码接入 + 主动停止 + 设备类型区分 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给局域网传输工具加三项体验改进——手机扫码接入、三种方式主动停止服务、客户端按设备类型区分显示。

**Architecture:** 后端在现有 httplib/SQLite 结构上扩展:新增 `util::lan_ips()` 枚举本机 IPv4、新增 `runstate` 模块读写 `run.json`(供 `--stop` 用)、`phones` 表加 `device` 列(ALTER 迁移)、`/api/self` 带出 `lan_ips`、新增 `POST /api/quit` 与信号处理统一走 `svr_.stop()` 退出。前端纯静态扩展:vendor 一个 MIT 二维码库离线生成二维码,新增扫码浮层与停止按钮,注册时按 UA 上送 `device`。

**Tech Stack:** C++17 / cpp-httplib / SQLite3 / nlohmann::json / doctest(单测) / 原生 JS(无构建)/ qrcode-generator(MIT, vendored)

---

## 文件结构

| 文件 | 职责 | 动作 |
|---|---|---|
| `src/util.h` / `src/util.cpp` | 新增 `lan_ips()` 枚举非回环 IPv4 | 修改 |
| `src/runstate.h` / `src/runstate.cpp` | 读写/删除 `run.json`(监听端口) | 新建 |
| `src/auth.h` / `src/auth.cpp` | `PhoneInfo.device` 字段、`phones.device` 列迁移、`normalize_device()` | 修改 |
| `src/server.h` / `src/server.cpp` | `request_stop()`、`run()` 加 run.json+信号+退出打印、`/api/self` 带 lan_ips、`/api/quit`、`/api/peers` 带 device | 修改 |
| `src/server_auth.cpp` | 注册时解析并存储 `device` | 修改 |
| `src/main.cpp` | `--stop` 分支 | 修改 |
| `CMakeLists.txt` | Windows 链接 `iphlpapi` | 修改 |
| `web/qrcode.js` | vendored 二维码库 | 新建 |
| `web/index.html` | 接入/停止按钮、扫码浮层、引入 qrcode.js | 修改 |
| `web/app.js` | 扫码渲染、停止、UA 设备类型、扫码自动填 PIN、列表图标 | 修改 |
| `web/style.css` | 按钮与浮层样式 | 修改 |
| `tests/test_util.cpp` | `lan_ips` 不含回环 | 修改 |
| `tests/test_runstate.cpp` | run.json 读写删 | 新建 |
| `tests/test_auth.cpp` | device 列、迁移、`normalize_device` | 修改 |
| `README.md` | 扫码接入 + 另一台电脑用浏览器接入说明 | 修改 |

> 注:CMake 用 `file(GLOB ...)`,新增 `.cpp` 自动纳入 `duoduan` 与 `unit_tests` 两个目标,无需手动登记。`src/main.cpp` 已从 `unit_tests` 排除,故 `--stop`/信号/HTTP 路由这类需要起进程的行为采用**手工验证**(项目现有测试均为单元级,无 HTTP 集成测试框架;且 Discovery 固定占用 UDP 38520,并行起多个 App 实例不可靠)。可单元测试的纯逻辑(lan_ips、runstate、device 迁移、normalize_device)全部走 TDD。

---

## Task 1: `util::lan_ips()` 枚举本机非回环 IPv4

**Files:**
- Modify: `src/util.h`
- Modify: `src/util.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_util.cpp`

- [ ] **Step 1: 在 test_util.cpp 末尾加失败测试**

```cpp
TEST_CASE("util::lan_ips 只返回非回环 IPv4") {
  auto ips = util::lan_ips();           // 不保证非空（CI 可能无网卡）
  for (const auto& ip : ips) {
    CHECK(ip.rfind("127.", 0) != 0);    // 不含回环
    CHECK(ip.find(':') == std::string::npos);  // 仅 IPv4
    CHECK_FALSE(ip.empty());
  }
}
```

- [ ] **Step 2: 在 util.h 声明**

在 `namespace util {` 内、`gen_secret();` 之后加:

```cpp
#include <vector>   // 放到文件顶部已有 include 区
```
并在 namespace 内加声明:
```cpp
// 枚举本机所有非回环 IPv4 地址（用于二维码/手机接入提示）
std::vector<std::string> lan_ips();
```

- [ ] **Step 3: 在 util.cpp 实现**

文件顶部 include 区(在现有 include 之后)加平台头:
```cpp
#include <cstdint>
#ifndef _WIN32
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#endif
```
在 `namespace util {` 内任意位置(如 `gen_secret` 之后)加实现:
```cpp
std::vector<std::string> lan_ips() {
  std::vector<std::string> ips;
#ifndef _WIN32
  struct ifaddrs* ifs = nullptr;
  if (getifaddrs(&ifs) != 0) return ips;
  for (auto* p = ifs; p; p = p->ifa_next) {
    if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
    auto* sin = reinterpret_cast<struct sockaddr_in*>(p->ifa_addr);
    if ((ntohl(sin->sin_addr.s_addr) >> 24) == 127) continue;  // 跳过 127.x
    char buf[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf))) ips.emplace_back(buf);
  }
  freeifaddrs(ifs);
#else
  ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
  ULONG sz = 15000;
  std::vector<char> buf(sz);
  auto* addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
  ULONG ret = GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &sz);
  if (ret == ERROR_BUFFER_OVERFLOW) {
    buf.resize(sz);
    addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    ret = GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &sz);
  }
  if (ret != NO_ERROR) return ips;
  for (auto* a = addrs; a; a = a->Next) {
    if (a->OperStatus != IfOperStatusUp) continue;
    for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
      if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
      auto* sin = reinterpret_cast<struct sockaddr_in*>(u->Address.lpSockaddr);
      if ((ntohl(sin->sin_addr.s_addr) >> 24) == 127) continue;
      char b[INET_ADDRSTRLEN] = {0};
      if (inet_ntop(AF_INET, &sin->sin_addr, b, sizeof(b))) ips.emplace_back(b);
    }
  }
#endif
  return ips;
}
```

- [ ] **Step 4: Windows 链接 iphlpapi**

在 `CMakeLists.txt` 两处 `if(WIN32)` 块里把 `ws2_32` 那行改为同时链接 `iphlpapi`:
```cmake
if(WIN32)
  target_link_libraries(duoduan PRIVATE ws2_32 iphlpapi)
endif()
```
```cmake
if(WIN32)
  target_link_libraries(unit_tests PRIVATE ws2_32 iphlpapi)
endif()
```

- [ ] **Step 5: 构建并跑测试**

Run:
```bash
cmake -S . -B build && cmake --build build -j && ./build/unit_tests -tc="util::lan_ips 只返回非回环 IPv4"
```
Expected: PASS(本机有网卡时会打印若干 IP 并通过;无网卡时空集合也通过)。

- [ ] **Step 6: 提交**

```bash
git add src/util.h src/util.cpp CMakeLists.txt tests/test_util.cpp
git commit -m "feat: util::lan_ips 枚举本机非回环 IPv4"
```

---

## Task 2: `runstate` 模块(run.json 读写删)

**Files:**
- Create: `src/runstate.h`
- Create: `src/runstate.cpp`
- Test: `tests/test_runstate.cpp`

- [ ] **Step 1: 写失败测试 `tests/test_runstate.cpp`**

```cpp
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
```

- [ ] **Step 2: 跑测试确认编译失败**

Run: `cmake --build build -j 2>&1 | head` — Expected: FAIL(`runstate.h` 不存在)。

- [ ] **Step 3: 创建 `src/runstate.h`**

```cpp
#pragma once
#include <filesystem>

// 运行态文件 run.json：记录当前实例的实际监听端口，供 `--stop` 自调用。
namespace runstate {
void write(const std::filesystem::path& file, int port);
int read_port(const std::filesystem::path& file);  // 缺失/损坏返回 0
void remove(const std::filesystem::path& file);
}
```

- [ ] **Step 4: 创建 `src/runstate.cpp`**

```cpp
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
```

- [ ] **Step 5: 构建并测试**

Run: `cmake -S . -B build && cmake --build build -j && ./build/unit_tests -tc="runstate 读写删 run.json"`
Expected: PASS

- [ ] **Step 6: 提交**

```bash
git add src/runstate.h src/runstate.cpp tests/test_runstate.cpp
git commit -m "feat: runstate 模块读写 run.json"
```

---

## Task 3: `phones.device` 列 + `normalize_device()`

**Files:**
- Modify: `src/auth.h`
- Modify: `src/auth.cpp`
- Test: `tests/test_auth.cpp`

- [ ] **Step 1: 在 test_auth.cpp 末尾加失败测试**

```cpp
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
```

在 test_auth.cpp 顶部 include 区补上(用于旧库测试):
```cpp
#include <sqlite3.h>
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build build -j 2>&1 | head` — Expected: FAIL(`normalize_device` 与 `PhoneInfo::device` 未声明)。

- [ ] **Step 3: 修改 `src/auth.h`**

`PhoneInfo` 结构体加字段:
```cpp
struct PhoneInfo {
  std::string id, name, token;
  long long last_seen = 0;
  std::string device = "phone";  // "phone" | "pc"
};
```
在 `struct PhoneInfo` 之后(类外)加自由函数声明:
```cpp
// 设备类型归一化：仅 "phone"/"pc"，其余一律按 "phone"
std::string normalize_device(const std::string& d);
```

- [ ] **Step 4: 修改 `src/auth.cpp`**

(a) 构造函数建表之后加迁移(在 `exec_sql(db_, R"sql(...)sql");` 之后):
```cpp
  // 迁移：旧库补 device 列（列已存在时 ALTER 失败，忽略错误）
  {
    char* err = nullptr;
    sqlite3_exec(db_, "ALTER TABLE phones ADD COLUMN device TEXT NOT NULL DEFAULT 'phone'",
                 nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
  }
```

(b) `row_to_phone` 读 device(在 `p.last_seen = ...` 之后):
```cpp
  p.device = txt(4);
  if (p.device.empty()) p.device = "phone";
```

(c) `save_phone` 改为含 device:
```cpp
void AuthStore::save_phone(const PhoneInfo& p) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(
      db_, "INSERT OR REPLACE INTO phones(id,name,token,last_seen,device) VALUES(?,?,?,?,?)",
      -1, &st, nullptr);
  sqlite3_bind_text(st, 1, p.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, p.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, p.token.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 4, p.last_seen);
  sqlite3_bind_text(st, 5, normalize_device(p.device).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(st);
  sqlite3_finalize(st);
}
```

(d) 三处 SELECT 把列加上 `device`(第 5 列,与 `row_to_phone` 的 `txt(4)` 对应):
- `find_phone_by_token`: `"SELECT id,name,token,last_seen,device FROM phones WHERE token=?"`
- `find_phone_by_id`: `"SELECT id,name,token,last_seen,device FROM phones WHERE id=?"`
- `phones()`: `"SELECT id,name,token,last_seen,device FROM phones ORDER BY name"`

(e) 文件末尾加 `normalize_device` 实现:
```cpp
std::string normalize_device(const std::string& d) {
  return d == "pc" ? "pc" : "phone";
}
```

- [ ] **Step 5: 构建并跑三个测试**

Run:
```bash
cmake --build build -j && ./build/unit_tests -tc="normalize_device 仅接受 phone/pc","AuthStore device 列读写默认值","AuthStore 旧库无 device 列自动迁移"
```
Expected: PASS

- [ ] **Step 6: 跑全量测试确保没破坏旧用例**

Run: `./build/unit_tests`
Expected: all PASS(原 AuthStore 用例仍通过)。

- [ ] **Step 7: 提交**

```bash
git add src/auth.h src/auth.cpp tests/test_auth.cpp
git commit -m "feat: phones 表加 device 列与 normalize_device，旧库自动迁移"
```

---

## Task 4: `/api/self` 带 lan_ips、注册存 device、`/api/peers` 带 device

**Files:**
- Modify: `src/server.cpp`(`/api/self`、`/api/peers`)
- Modify: `src/server_auth.cpp`(注册)

- [ ] **Step 1: `/api/self` 加 lan_ips**

`src/server.cpp` 的 `/api/self` 处理函数里,把:
```cpp
    if (is_local(req)) j["pin"] = cfg_.pin;
```
改为:
```cpp
    if (is_local(req)) {
      j["pin"] = cfg_.pin;
      j["lan_ips"] = util::lan_ips();  // 远程不返回（与 pin 同等待遇）
    }
```

- [ ] **Step 2: `/api/peers` 手机项带 device**

`src/server.cpp` 的 `/api/peers` 里,手机循环 `for (auto& ph : auth_.phones())` 的 `arr.push_back({...})` 加一项 `device`:
```cpp
      arr.push_back({{"id", ph.id},
                     {"name", ph.name},
                     {"type", "phone"},
                     {"device", ph.device},
                     {"paired", true},
                     {"online", now - ph.last_seen < 30000}});
```

- [ ] **Step 3: 注册解析并存储 device**

`src/server_auth.cpp` 的 `/api/phone/register` 里,解析 `pin` 的 try 块加上 `device`:
```cpp
    std::string phone_id, name, pin, device;
    try {
      phone_id = j.value("phone_id", "");
      name = j.value("name", "");
      pin = j.value("pin", "");
      device = j.value("device", "");
    } catch (const nlohmann::json::exception&) { res.status = 400; return; }
```
并在构造 `PhoneInfo p;` 处设置(在 `p.last_seen = now;` 之后):
```cpp
    p.device = normalize_device(device);
```

- [ ] **Step 4: 构建**

Run: `cmake --build build -j`
Expected: 编译通过(`util::lan_ips`/`normalize_device` 已在前序 Task 提供)。

- [ ] **Step 5: 提交**

```bash
git add src/server.cpp src/server_auth.cpp
git commit -m "feat: /api/self 带 lan_ips，注册存 device，/api/peers 带 device"
```

---

## Task 5: 退出流程 —— `App::request_stop()`、`run()` 重写、`/api/quit`

**Files:**
- Modify: `src/server.h`
- Modify: `src/server.cpp`

- [ ] **Step 1: server.h 声明 `request_stop`**

`class App` 的 `public:` 区(在 `int port() const ...` 之后)加:
```cpp
  void request_stop();  // 触发优雅退出（信号处理/quit 路由调用）
```

- [ ] **Step 2: server.cpp 顶部加头与全局信号桥**

文件顶部 include 区加:
```cpp
#include <csignal>
#include "runstate.h"
```
在 `using nlohmann::json;` 之后加匿名命名空间:
```cpp
namespace {
App* g_app = nullptr;  // 仅供信号处理回调使用
void on_signal(int) {
  if (g_app) g_app->request_stop();
}
}  // namespace
```

- [ ] **Step 3: 实现 `request_stop` 并重写 `run()`**

把现有 `App::run()` 的结尾(从 `setup_routes();` 到 `return svr_.listen_after_bind();`)替换为:
```cpp
  setup_routes();
  runstate::write(cfg_.data_dir / "run.json", port_);
  g_app = this;
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  std::printf("已启动：http://localhost:%d  （设备名：%s，配对 PIN：%s）\n", port_,
              cfg_.name.c_str(), cfg_.pin.c_str());
  bool ok = svr_.listen_after_bind();  // 阻塞，stop() 后返回
  if (discovery_) discovery_->stop();
  runstate::remove(cfg_.data_dir / "run.json");
  std::printf("已停止\n");
  g_app = nullptr;
  return ok;
```
(注:原来的 `std::printf("已启动...")` 旧行被上面这段取代,不要保留重复。)

在 `App::run()` 之后新增方法实现:
```cpp
void App::request_stop() { svr_.stop(); }
```

- [ ] **Step 4: 新增 `/api/quit` 路由**

`setup_routes()` 里,在 `setup_auth_routes();` 之前加:
```cpp
  // 主动停止：仅 localhost（远程经 pre-routing 已 403，此处再防一层）
  svr_.Post("/api/quit", [this](const httplib::Request& req, httplib::Response& res) {
    if (!is_local(req)) { res.status = 403; return; }
    res.set_content("{\"ok\":true}", "application/json");
    std::thread([this] { request_stop(); }).detach();  // 不能在 handler 内直接停
  });
```
(`<thread>` 已在 server.cpp 顶部 include。)

- [ ] **Step 5: 构建**

Run: `cmake --build build -j`
Expected: 编译通过。

- [ ] **Step 6: 手工验证退出流程**

Run(终端 A):
```bash
./build/duoduan --data-dir /tmp/dd_quit --port 8600
```
观察打印「已启动…」,并确认 `/tmp/dd_quit/run.json` 存在(内容含 `"port":8600`)。

Run(终端 B,验证 quit + 远程 403):
```bash
curl -s -X POST http://127.0.0.1:8600/api/quit          # 期望 {"ok":true}
```
回到终端 A:应打印「已停止」并退出;确认 `run.json` 已删除。

远程 403 验证(另起实例后用非回环地址打,或确认 pre-routing):
```bash
# 重新启动后，用本机 LAN IP 而非 127.0.0.1 访问应 403
curl -s -o /dev/null -w "%{http_code}\n" -X POST http://<本机LAN_IP>:8600/api/quit  # 期望 403
```

- [ ] **Step 7: 验证 Ctrl+C / SIGTERM**

Run: 再次启动 `./build/duoduan --data-dir /tmp/dd_quit --port 8600`,按 `Ctrl+C` → 应打印「已停止」并退出,`run.json` 被删。
再启动一次,另开终端 `kill -TERM <pid>` → 同样「已停止」+清理。

- [ ] **Step 8: 提交**

```bash
git add src/server.h src/server.cpp
git commit -m "feat: 优雅退出（信号/quit 路由）+ run.json 生命周期"
```

---

## Task 6: `./duoduan --stop`

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: 重写 main.cpp**

整体替换为(在原基础上前置 `--stop` 分支,放在 `Config::load` 之前以免产生副作用):
```cpp
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>
#include "config.h"
#include "httplib.h"
#include "runstate.h"
#include "server.h"
#include "util.h"

int main(int argc, char** argv) {
  namespace fs = std::filesystem;
  fs::path data_dir = util::home_dir() / ".duoduan";
  int port_override = 0;
  std::string name_override, download_override;
  bool stop_flag = false;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--stop")) stop_flag = true;
    if (i + 1 < argc) {
      if (!std::strcmp(argv[i], "--data-dir")) data_dir = argv[i + 1];
      if (!std::strcmp(argv[i], "--port")) port_override = std::atoi(argv[i + 1]);
      if (!std::strcmp(argv[i], "--name")) name_override = argv[i + 1];
      if (!std::strcmp(argv[i], "--download-dir")) download_override = argv[i + 1];
    }
  }

  if (stop_flag) {
    auto run_file = data_dir / "run.json";
    int port = runstate::read_port(run_file);
    if (!port) {
      std::printf("服务未在运行\n");
      runstate::remove(run_file);
      return 0;
    }
    httplib::Client cli("127.0.0.1", port);
    cli.set_connection_timeout(2);
    auto r = cli.Post("/api/quit", "{}", "application/json");
    if (!r) {
      std::printf("服务未在运行\n");
      runstate::remove(run_file);
      return 0;
    }
    for (int i = 0; i < 50; ++i) {  // 轮询直到端口拒绝连接
      httplib::Client probe("127.0.0.1", port);
      probe.set_connection_timeout(1);
      if (!probe.Get("/api/self")) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::printf("已停止\n");
    return 0;
  }

  Config cfg = Config::load(data_dir);
  if (port_override) cfg.port = port_override;
  if (!name_override.empty()) cfg.name = name_override;
  if (!download_override.empty()) cfg.download_dir = download_override;

  fs::path web_dir = "web";
  if (!fs::exists(web_dir / "index.html") && fs::exists("../web/index.html"))
    web_dir = "../web";  // 支持从 build/ 目录运行

  App app(std::move(cfg), web_dir);
  return app.run() ? 0 : 1;
}
```

- [ ] **Step 2: 构建**

Run: `cmake --build build -j`
Expected: 编译通过。

- [ ] **Step 3: 手工验证 --stop（运行中）**

终端 A:`./build/duoduan --data-dir /tmp/dd_stop --port 8601`
终端 B:`./build/duoduan --data-dir /tmp/dd_stop --stop`
Expected:终端 B 打印「已停止」;终端 A 也打印「已停止」并退出;`run.json` 消失。

- [ ] **Step 4: 手工验证 --stop（未运行）**

Run: `./build/duoduan --data-dir /tmp/dd_stop --stop`(此时无进程)
Expected: 打印「服务未在运行」,退出码 0。

- [ ] **Step 5: 验证残留 run.json**

```bash
mkdir -p /tmp/dd_stale && echo '{"port":59999}' > /tmp/dd_stale/run.json
./build/duoduan --data-dir /tmp/dd_stale --stop
```
Expected: 端口连不上 → 打印「服务未在运行」,且 `/tmp/dd_stale/run.json` 被删除。

- [ ] **Step 6: 提交**

```bash
git add src/main.cpp
git commit -m "feat: ./duoduan --stop 通过 HTTP 自调用停止服务"
```

---

## Task 7: 前端 —— 二维码接入、停止按钮、设备类型、扫码自动填 PIN

**Files:**
- Create: `web/qrcode.js`(vendored)
- Modify: `web/index.html`
- Modify: `web/app.js`
- Modify: `web/style.css`

- [ ] **Step 1: vendor 二维码库(MIT)**

Run:
```bash
curl -fsSL https://cdn.jsdelivr.net/npm/qrcode-generator@1.4.4/qrcode.js -o web/qrcode.js
head -3 web/qrcode.js   # 确认是 qrcode-generator 源码（含版权与 MIT 字样）
```
说明:qrcode-generator(Kazuhiko Arase, MIT),暴露全局 `qrcode(typeNumber, ecc)`,离线生成,无网络依赖。若无网络环境,从任一可信镜像取同版本文件放到 `web/qrcode.js` 即可。

- [ ] **Step 2: index.html 引入库 + 加按钮与浮层**

在 `#self-pin` 的 `</div>` 之后(仍在 `#sidebar` 内,`#peer-list` 之前)加:
```html
  <div id="self-actions">
    <button id="qr-btn" hidden>📱 手机接入</button>
    <button id="quit-btn" hidden>⏻ 停止服务</button>
  </div>
```
在 `<div id="login" ...>` 块之后、`<script src="app.js">` 之前加扫码浮层:
```html
<div id="qr-modal" hidden>
  <div id="qr-box">
    <h3>手机扫码接入</h3>
    <div id="qr-img"></div>
    <select id="qr-ip" hidden></select>
    <div id="qr-url"></div>
    <div id="qr-hint">用手机相机/浏览器扫码；需与本机在同一 Wi-Fi。扫码后填设备名即可连接。</div>
    <button id="qr-close">关闭</button>
  </div>
</div>
```
把底部脚本改为先引入二维码库:
```html
<script src="qrcode.js"></script>
<script src="app.js"></script>
```

- [ ] **Step 3: app.js —— 注册带 device + 扫码自动填 PIN**

(a) `showLogin()` 改为(自动填 PIN、聚焦设备名、成功后抹掉 URL 上的 pin):
```javascript
function showLogin() {
  $('#login').hidden = false;
  const urlPin = new URLSearchParams(location.search).get('pin');
  if (urlPin && /^\d{6}$/.test(urlPin)) {
    $('#login-pin').value = urlPin;
    setTimeout(() => $('#login-name').focus(), 0);
  }
  $('#login-btn').onclick = async () => {
    const name = $('#login-name').value.trim();
    const pin = $('#login-pin').value.trim();
    if (!name || !/^\d{6}$/.test(pin)) {
      $('#login-err').textContent = '请填写设备名和 6 位 PIN';
      return;
    }
    const device = /Mobi|Android|iPhone/i.test(navigator.userAgent) ? 'phone' : 'pc';
    try {
      await api('/api/phone/register', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ phone_id: phoneId(), name, pin, device }),
      });
      location.replace('/');  // 抹掉带 PIN 的地址
    } catch (e) {
      $('#login-err').textContent = String(e).includes('429')
        ? '尝试次数过多，请稍后再试' : 'PIN 错误，请核对电脑屏幕上的数字';
    }
  };
}
```

(b) `renderPeers()` 里的 `name.textContent = ...` 一行改为按设备类型(电脑 💻,手机 📱):
```javascript
    const isPc = p.type === 'pc' || p.device === 'pc';
    name.textContent = (isPc ? '💻 ' : '📱 ') + p.name + (p.paired ? '' : ' 🔒');
```

- [ ] **Step 4: app.js —— 扫码浮层与停止按钮逻辑**

在 `renderPeers` 之后(或文件靠后位置)新增函数:
```javascript
function makeQR(text) {
  const qr = qrcode(0, 'M');  // type 0 = 自动选版本
  qr.addData(text);
  qr.make();
  return qr.createDataURL(6);  // 每格 6px
}

function showQR() {
  const ips = self.lan_ips || [];
  const sel = $('#qr-ip');
  const render = (ip) => {
    const url = `http://${ip}:${self.port}/?pin=${self.pin}`;
    $('#qr-url').textContent = url;
    $('#qr-img').innerHTML = '';
    const img = new Image();
    img.src = makeQR(url);
    $('#qr-img').appendChild(img);
  };
  if (!ips.length) {
    sel.hidden = true;
    $('#qr-img').innerHTML = '';
    $('#qr-url').textContent = '未检测到局域网 IP，请确认已连接 Wi-Fi 或网线';
  } else {
    sel.hidden = ips.length < 2;
    sel.innerHTML = '';
    ips.forEach((ip) => {
      const o = document.createElement('option');
      o.value = ip; o.textContent = ip;
      sel.appendChild(o);
    });
    sel.onchange = () => render(sel.value);
    render(ips[0]);
  }
  $('#qr-modal').hidden = false;
}

async function quitService() {
  if (!confirm('确定要停止服务吗？停止后本页和所有手机都将断开。')) return;
  try { await fetch('/api/quit', { method: 'POST' }); } catch (e) { /* 服务关闭会断连，忽略 */ }
  document.body.innerHTML =
    '<div style="padding:48px;text-align:center;font-size:18px;color:#333">服务已停止，可关闭本页。</div>';
}
```

- [ ] **Step 5: app.js —— 桌面模式下显示并绑定两个按钮**

在文件末尾启动 IIFE 的 `else`(桌面)分支里,`$('#self-pin').textContent = ...` 之后加:
```javascript
    $('#qr-btn').hidden = false;
    $('#quit-btn').hidden = false;
    $('#qr-btn').onclick = showQR;
    $('#quit-btn').onclick = quitService;
    $('#qr-close').onclick = () => { $('#qr-modal').hidden = true; };
```

- [ ] **Step 6: style.css 追加样式**

在 `web/style.css` 末尾追加:
```css
#self-actions { margin: 8px 0; display: flex; gap: 8px; flex-wrap: wrap; }
#self-actions button {
  font-size: 13px; padding: 6px 10px; border: 1px solid #ccc;
  border-radius: 6px; background: #fff; cursor: pointer;
}
#self-actions button:hover { background: #f0f0f0; }
#quit-btn { color: #c0392b; border-color: #e0b4ae; }

#qr-modal {
  position: fixed; inset: 0; background: rgba(0,0,0,.45);
  display: flex; align-items: center; justify-content: center; z-index: 100;
}
#qr-box {
  background: #fff; padding: 24px 28px; border-radius: 12px;
  text-align: center; max-width: 90vw;
}
#qr-box h3 { margin: 0 0 12px; }
#qr-img img { width: 220px; height: 220px; image-rendering: pixelated; }
#qr-ip { margin: 10px 0; padding: 4px; }
#qr-url { font-family: monospace; font-size: 13px; word-break: break-all; margin: 8px 0; color: #555; }
#qr-hint { font-size: 12px; color: #888; margin-bottom: 12px; }
#qr-close { padding: 6px 18px; border: 1px solid #ccc; border-radius: 6px; background: #fff; cursor: pointer; }
```
(若 `#qr-modal[hidden]`/`[hidden]` 守卫已存在于全局,无需重复;项目已有全局 `[hidden]{display:none!important}` 守卫——见 commit 7799bfb——故 `hidden` 属性可靠生效。)

- [ ] **Step 7: 构建并手工验证前端**

Run: `cmake --build build -j && ./build/duoduan --data-dir /tmp/dd_fe --port 8602`
浏览器开 `http://localhost:8602/`:
- 侧栏出现「📱 手机接入」「⏻ 停止服务」两个按钮;
- 点「手机接入」→ 弹出二维码 + 网址(`http://<IP>:8602/?pin=<6位>`);多网卡时有 IP 下拉;
- 手机(或另一浏览器隐身窗)扫码/开该网址 → 登录层 PIN 已自动填好,只需填设备名 → 连接 → 地址栏 pin 参数消失;
- 用台式机浏览器(非手机 UA)接入 → 电脑端设备列表里该项显示 💻;手机 UA 接入显示 📱;
- 点「停止服务」→ 确认 → 页面变「服务已停止」,后台进程退出。

- [ ] **Step 8: 提交**

```bash
git add web/qrcode.js web/index.html web/app.js web/style.css
git commit -m "feat: 前端二维码接入、停止按钮、设备类型图标、扫码自动填 PIN"
```

---

## Task 8: README 文档更新

**Files:**
- Modify: `README.md`

- [ ] **Step 1: 补充使用说明**

在 README 的"运行/配对"相关章节补两段:
- **手机接入**:电脑端网页点「📱 手机接入」,手机扫码或打开显示的网址,填设备名即连(PIN 已由二维码带入)。
- **另一台电脑接入**:无需安装本程序,浏览器直接打开服务器网址(`http://<服务器IP>:8520/`),按提示输入 PIN 即可作为客户端使用;列表中会以 💻 区分电脑、📱 区分手机。
- **停止服务**:三种方式 —— 网页「⏻ 停止服务」按钮、运行终端 `Ctrl+C`、或另开终端执行 `./duoduan --stop`。

- [ ] **Step 2: 提交**

```bash
git add README.md
git commit -m "docs: README 补充扫码接入、浏览器接入与停止服务说明"
```

---

## 完成标准

- `./build/unit_tests` 全绿(含新增 lan_ips / runstate / device 用例)。
- 三种停止方式均能让进程打印「已停止」并清理 `run.json`。
- 手机扫码后 PIN 自动填入、注册成功后地址栏不残留 PIN。
- 设备列表按 💻/📱 正确区分电脑与手机客户端。
- 远程访问 `/api/quit`、远程 `/api/self` 不含 `lan_ips`/`pin`(鉴权边界不回退)。
