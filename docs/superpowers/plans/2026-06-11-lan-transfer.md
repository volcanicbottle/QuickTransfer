# 局域网文件传输工具 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现一个类似微信文件传输助手的局域网互传工具：C++ 单可执行文件，浏览器聊天界面，支持设备自动发现、文字消息、文件传输（含进度/重试）和 SQLite 历史记录。

**Architecture:** 每台电脑运行同一个程序，内含 HTTP 服务器（给浏览器提供界面与 API、给其他电脑提供接收接口）、HTTP 客户端（向其他电脑推送）和 UDP 广播发现。浏览器通过 SSE 接收实时事件。

**Tech Stack:** C++17、CMake、cpp-httplib v0.15.3（HTTP 服务端+客户端）、nlohmann/json v3.11.3、SQLite 3.45.1（amalgamation）、doctest v2.4.11（测试）、纯 HTML/CSS/JS 前端。

**设计文档:** `docs/superpowers/specs/2026-06-10-lan-transfer-design.md`

**里程碑映射:** M1=任务1–5、8；M2=任务6、7、9；M3=任务10–13；M4=任务14。
（注：history 模块按依赖关系提前到 M2 之前实现。）

---

## 项目结构

```
CMakeLists.txt
.gitignore
third_party/httplib.h json.hpp doctest.h sqlite3.c sqlite3.h
src/
  util.h/.cpp        # gen_id、home_dir、unique_path、url_encode、now_ms
  config.h/.cpp      # 设备ID/名称/端口/目录，config.json 持久化
  discovery.h/.cpp   # UDP 广播发现（纯函数 + 运行时线程）
  history.h/.cpp     # SQLite 消息历史
  events.h/.cpp      # SSE 事件总线
  server.h/.cpp      # App：HTTP 路由 + 收发逻辑
  main.cpp           # 参数解析 + 启动
web/
  index.html style.css app.js
tests/
  test_main.cpp test_util.cpp test_config.cpp test_discovery.cpp
  test_history.cpp test_events.cpp
```

约定：所有构建命令在仓库根目录执行；`cmake -B build && cmake --build build -j`（CMake 用了 GLOB，新增源文件后必须重新跑 `cmake -B build`）。

---

### Task 1: 项目脚手架与第三方依赖

**Files:**
- Create: `.gitignore`, `CMakeLists.txt`, `src/main.cpp`
- Create: `third_party/httplib.h`, `third_party/json.hpp`, `third_party/doctest.h`, `third_party/sqlite3.c`, `third_party/sqlite3.h`

- [ ] **Step 1: 创建目录与 .gitignore**

```bash
mkdir -p src web tests third_party
cat > .gitignore <<'EOF'
build/
EOF
```

- [ ] **Step 2: 下载第三方库（版本已固定）**

```bash
curl -L -o third_party/httplib.h https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.15.3/httplib.h
curl -L -o third_party/json.hpp https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
curl -L -o third_party/doctest.h https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h
curl -L -o /tmp/sqlite.zip https://sqlite.org/2024/sqlite-amalgamation-3450100.zip
unzip -o /tmp/sqlite.zip -d /tmp/
cp /tmp/sqlite-amalgamation-3450100/sqlite3.c /tmp/sqlite-amalgamation-3450100/sqlite3.h third_party/
```

验证：`ls -la third_party/` 应有 5 个文件，httplib.h 约 350KB+，sqlite3.c 约 8MB。

- [ ] **Step 3: 写 CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)
project(duoduan CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/utf-8)
endif()

add_library(sqlite3_lib STATIC third_party/sqlite3.c)
target_include_directories(sqlite3_lib PUBLIC third_party)

find_package(Threads REQUIRED)

file(GLOB APP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_executable(duoduan ${APP_SOURCES})
target_include_directories(duoduan PRIVATE src third_party)
target_link_libraries(duoduan PRIVATE sqlite3_lib Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
  target_link_libraries(duoduan PRIVATE ws2_32)
endif()
```

- [ ] **Step 4: 写最小 main.cpp**

```cpp
#include <cstdio>

int main() {
  std::printf("多端互通 启动占位\n");
  return 0;
}
```

- [ ] **Step 5: 构建并运行验证**

```bash
cmake -B build && cmake --build build -j
./build/duoduan
```

预期输出：`多端互通 启动占位`

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "chore: 项目脚手架与第三方依赖（cpp-httplib/json/doctest/sqlite）"
```

---

### Task 2: 测试框架接入

**Files:**
- Modify: `CMakeLists.txt`（末尾追加）
- Create: `tests/test_main.cpp`

- [ ] **Step 1: tests/test_main.cpp**

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

TEST_CASE("测试框架可用") {
  CHECK(1 + 1 == 2);
}
```

- [ ] **Step 2: CMakeLists.txt 末尾追加测试目标**

```cmake
enable_testing()
file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
set(SRC_NO_MAIN ${APP_SOURCES})
list(REMOVE_ITEM SRC_NO_MAIN ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
add_executable(unit_tests ${TEST_SOURCES} ${SRC_NO_MAIN})
target_include_directories(unit_tests PRIVATE src third_party)
target_link_libraries(unit_tests PRIVATE sqlite3_lib Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
  target_link_libraries(unit_tests PRIVATE ws2_32)
endif()
add_test(NAME unit_tests COMMAND unit_tests)
```

注意：`SRC_NO_MAIN` 依赖上文的 `APP_SOURCES` GLOB，所以这段必须放在 `add_executable(duoduan ...)` 之后。

- [ ] **Step 3: 构建并运行测试**

```bash
cmake -B build && cmake --build build -j
./build/unit_tests
```

预期：`[doctest] ... 1 passed`（test cases: 1 | 1 passed）

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "test: 接入 doctest 测试框架"
```

---

### Task 3: util 模块（TDD）

**Files:**
- Create: `src/util.h`, `src/util.cpp`, `tests/test_util.cpp`

- [ ] **Step 1: 写失败的测试 tests/test_util.cpp**

```cpp
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
```

- [ ] **Step 2: 创建空的 src/util.h（只放声明）和空 util.cpp，确认测试编译失败或断言失败**

src/util.h:

```cpp
#pragma once
#include <filesystem>
#include <string>

namespace util {
std::string gen_id();                 // 16 位十六进制随机ID
std::filesystem::path home_dir();     // HOME / USERPROFILE
std::filesystem::path unique_path(const std::filesystem::path& dir,
                                  const std::string& filename);  // 重名追加 " (n)"
std::string url_encode(const std::string& s);
long long now_ms();
}
```

src/util.cpp 先写空实现骨架（返回空值），运行：

```bash
cmake -B build && cmake --build build -j && ./build/unit_tests
```

预期：FAIL（gen_id 长度断言失败等）

- [ ] **Step 3: 实现 src/util.cpp**

```cpp
#include "util.h"
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <random>
#include <sstream>

namespace util {

std::string gen_id() {
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uint64_t> d;
  std::ostringstream os;
  os << std::hex << d(gen);
  std::string s = os.str();
  while (s.size() < 16) s = "0" + s;
  return s;
}

std::filesystem::path home_dir() {
  const char* h = std::getenv("HOME");
  if (!h) h = std::getenv("USERPROFILE");
  return h ? std::filesystem::path(h) : std::filesystem::current_path();
}

std::filesystem::path unique_path(const std::filesystem::path& dir,
                                  const std::string& filename) {
  namespace fs = std::filesystem;
  fs::path p = dir / filename;
  if (!fs::exists(p)) return p;
  std::string stem = fs::path(filename).stem().string();
  std::string ext = fs::path(filename).extension().string();
  for (int i = 1;; ++i) {
    fs::path cand = dir / (stem + " (" + std::to_string(i) + ")" + ext);
    if (!fs::exists(cand)) return cand;
  }
}

std::string url_encode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

long long now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace util
```

- [ ] **Step 4: 运行测试确认通过**

```bash
cmake -B build && cmake --build build -j && ./build/unit_tests
```

预期：全部 PASS

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: util 模块（ID生成/重名文件/URL编码/时间戳）"
```

---

### Task 4: config 模块（TDD）

**Files:**
- Create: `src/config.h`, `src/config.cpp`, `tests/test_config.cpp`

- [ ] **Step 1: 写失败的测试 tests/test_config.cpp**

```cpp
#include "doctest.h"
#include "config.h"
#include "util.h"
#include <filesystem>

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
```

- [ ] **Step 2: src/config.h**

```cpp
#pragma once
#include <filesystem>
#include <string>

struct Config {
  std::string id;
  std::string name;
  int port = 8520;
  std::filesystem::path data_dir;       // 运行时指定，不序列化
  std::filesystem::path download_dir;

  std::filesystem::path staging_dir() const { return data_dir / "staging"; }
  std::filesystem::path db_path() const { return data_dir / "history.db"; }

  // 从 data_dir/config.json 加载；不存在则生成默认值并保存
  static Config load(const std::filesystem::path& data_dir);
  void save() const;
};
```

- [ ] **Step 3: 运行测试确认编译失败**

```bash
cmake -B build && cmake --build build -j 2>&1 | tail -5
```

预期：链接错误 `undefined reference to Config::load`

- [ ] **Step 4: 实现 src/config.cpp**

```cpp
#include "config.h"
#include "json.hpp"
#include "util.h"
#include <fstream>
#ifndef _WIN32
#include <unistd.h>
#endif

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
  c.download_dir = util::home_dir() / "Downloads" / "多端互通";
  fs::create_directories(data_dir);
  auto file = data_dir / "config.json";
  if (fs::exists(file)) {
    std::ifstream in(file);
    auto j = nlohmann::json::parse(in, nullptr, false);
    if (!j.is_discarded()) {
      c.id = j.value("id", c.id);
      c.name = j.value("name", c.name);
      c.port = j.value("port", c.port);
      if (j.contains("download_dir"))
        c.download_dir = fs::path(j["download_dir"].get<std::string>());
    }
  }
  c.save();
  return c;
}

void Config::save() const {
  nlohmann::json j{{"id", id},
                   {"name", name},
                   {"port", port},
                   {"download_dir", download_dir.string()}};
  std::ofstream(data_dir / "config.json") << j.dump(2);
}
```

- [ ] **Step 5: 运行测试确认通过**

```bash
cmake -B build && cmake --build build -j && ./build/unit_tests
```

预期：全部 PASS

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: config 模块（设备ID/名称/端口持久化）"
```

---

### Task 5: discovery 模块（报文纯函数 TDD + UDP 运行时）

**Files:**
- Create: `src/discovery.h`, `src/discovery.cpp`, `tests/test_discovery.cpp`

- [ ] **Step 1: 写失败的测试 tests/test_discovery.cpp（只测纯函数）**

```cpp
#include "doctest.h"
#include "discovery.h"

TEST_CASE("announce 报文序列化与解析往返") {
  auto pkt = discovery::make_announce("abc123", "我的电脑", 8521);
  PeerInfo p;
  REQUIRE(discovery::parse_announce(pkt, p));
  CHECK(p.id == "abc123");
  CHECK(p.name == "我的电脑");
  CHECK(p.port == 8521);
}

TEST_CASE("parse_announce 拒绝非法报文") {
  PeerInfo p;
  CHECK_FALSE(discovery::parse_announce("not json", p));
  CHECK_FALSE(discovery::parse_announce(R"({"app":"other","id":"x","name":"n","port":1})", p));
  CHECK_FALSE(discovery::parse_announce(R"({"app":"duoduan","id":"","name":"n","port":1})", p));
  CHECK_FALSE(discovery::parse_announce(R"({"app":"duoduan","id":"x","name":"n","port":0})", p));
}
```

- [ ] **Step 2: src/discovery.h**

```cpp
#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct PeerInfo {
  std::string id, name, ip;
  int port = 0;
  long long last_seen = 0;  // util::now_ms() 时间戳
};

namespace discovery {
// 纯函数（可单测）
std::string make_announce(const std::string& id, const std::string& name, int port);
bool parse_announce(const std::string& packet, PeerInfo& out);  // 不填 ip/last_seen

constexpr int kUdpPort = 38520;
constexpr long long kOfflineMs = 10000;
}

// UDP 广播发现：每3秒广播一次自己，监听线程维护设备表
class Discovery {
 public:
  Discovery(std::string self_id, std::string self_name, int http_port);
  ~Discovery();
  void start(std::function<void()> on_change);  // 设备上线/信息变化时回调
  void stop();
  std::vector<PeerInfo> peers() const;
  bool find(const std::string& id, PeerInfo& out) const;

 private:
  void broadcast_loop();
  void listen_loop();

  std::string self_id_, self_name_;
  int http_port_;
  std::function<void()> on_change_;
  std::atomic<bool> running_{false};
  std::thread bth_, lth_;
  mutable std::mutex mu_;
  std::map<std::string, PeerInfo> peers_;
};
```

- [ ] **Step 3: 运行确认编译/链接失败**

```bash
cmake -B build && cmake --build build -j 2>&1 | tail -5
```

预期：`undefined reference to discovery::make_announce` 等

- [ ] **Step 4: 实现 src/discovery.cpp**

```cpp
#include "discovery.h"
#include "json.hpp"
#include "util.h"
#include <chrono>
#include <cstring>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
static void sock_init() { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); }
static void sock_close(sock_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
static void sock_init() {}
static void sock_close(sock_t s) { close(s); }
#endif

namespace discovery {

std::string make_announce(const std::string& id, const std::string& name, int port) {
  return nlohmann::json{{"app", "duoduan"}, {"id", id}, {"name", name}, {"port", port}}
      .dump();
}

bool parse_announce(const std::string& packet, PeerInfo& out) {
  auto j = nlohmann::json::parse(packet, nullptr, false);
  if (j.is_discarded() || j.value("app", "") != "duoduan") return false;
  out.id = j.value("id", "");
  out.name = j.value("name", "");
  out.port = j.value("port", 0);
  return !out.id.empty() && out.port > 0;
}

}  // namespace discovery

Discovery::Discovery(std::string self_id, std::string self_name, int http_port)
    : self_id_(std::move(self_id)),
      self_name_(std::move(self_name)),
      http_port_(http_port) {}

Discovery::~Discovery() { stop(); }

void Discovery::start(std::function<void()> on_change) {
  on_change_ = std::move(on_change);
  sock_init();
  running_ = true;
  bth_ = std::thread([this] { broadcast_loop(); });
  lth_ = std::thread([this] { listen_loop(); });
}

void Discovery::stop() {
  if (!running_) return;
  running_ = false;
  if (bth_.joinable()) bth_.join();
  if (lth_.joinable()) lth_.join();
}

void Discovery::broadcast_loop() {
  sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
  int yes = 1;
  setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof(yes));
  std::string msg = discovery::make_announce(self_id_, self_name_, http_port_);
  sockaddr_in bcast{};
  bcast.sin_family = AF_INET;
  bcast.sin_port = htons(discovery::kUdpPort);
  bcast.sin_addr.s_addr = INADDR_BROADCAST;
  sockaddr_in loop = bcast;
  loop.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 同机多实例测试用
  while (running_) {
    sendto(s, msg.data(), (int)msg.size(), 0, (sockaddr*)&bcast, sizeof(bcast));
    sendto(s, msg.data(), (int)msg.size(), 0, (sockaddr*)&loop, sizeof(loop));
    for (int i = 0; i < 30 && running_; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  sock_close(s);
}

void Discovery::listen_loop() {
  sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
  int yes = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
#ifdef SO_REUSEPORT
  setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (const char*)&yes, sizeof(yes));  // 同机多实例
#endif
#ifdef _WIN32
  DWORD tv = 1000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
  timeval tv{1, 0};
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(discovery::kUdpPort);
  addr.sin_addr.s_addr = INADDR_ANY;
  bind(s, (sockaddr*)&addr, sizeof(addr));
  char buf[1500];
  while (running_) {
    sockaddr_in from{};
    socklen_t flen = sizeof(from);
    int n = (int)recvfrom(s, buf, sizeof(buf), 0, (sockaddr*)&from, &flen);
    if (n <= 0) continue;
    PeerInfo p;
    if (!discovery::parse_announce(std::string(buf, n), p)) continue;
    if (p.id == self_id_) continue;
    char ipbuf[64] = {0};
    inet_ntop(AF_INET, &from.sin_addr, ipbuf, sizeof(ipbuf));
    p.ip = ipbuf;
    p.last_seen = util::now_ms();
    bool changed = false;
    {
      std::lock_guard<std::mutex> lk(mu_);
      auto it = peers_.find(p.id);
      changed = it == peers_.end() ||
                util::now_ms() - it->second.last_seen > discovery::kOfflineMs ||
                it->second.name != p.name || it->second.ip != p.ip ||
                it->second.port != p.port;
      peers_[p.id] = p;
    }
    if (changed && on_change_) on_change_();
  }
  sock_close(s);
}

std::vector<PeerInfo> Discovery::peers() const {
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<PeerInfo> v;
  for (auto& kv : peers_) v.push_back(kv.second);
  return v;
}

bool Discovery::find(const std::string& id, PeerInfo& out) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = peers_.find(id);
  if (it == peers_.end()) return false;
  out = it->second;
  return true;
}
```

注意（Windows）：`socklen_t` 在 winsock 里是 `int`。如果 MinGW 编译报 `socklen_t` 未定义，在 `#ifdef _WIN32` 块里加 `using socklen_t = int;`。

- [ ] **Step 5: 运行测试确认通过**

```bash
cmake -B build && cmake --build build -j && ./build/unit_tests
```

预期：全部 PASS（运行时类不在单测覆盖内，任务 8 做双实例集成验证）

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: UDP 广播设备发现"
```

---

### Task 6: history 模块（TDD）

**Files:**
- Create: `src/history.h`, `src/history.cpp`, `tests/test_history.cpp`

- [ ] **Step 1: 写失败的测试 tests/test_history.cpp**

```cpp
#include "doctest.h"
#include "history.h"
#include "util.h"
#include <filesystem>

TEST_CASE("History 增/查/改状态") {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / ("dd_db_" + util::gen_id());
  fs::create_directories(dir);
  {
    History h(dir / "t.db");
    Message m;
    m.peer_id = "p1";
    m.direction = "out";
    m.kind = "text";
    m.body = "你好";
    m.status = "pending";
    h.add(m);
    CHECK(m.id > 0);
    CHECK(m.ts > 0);

    auto list = h.list("p1");
    REQUIRE(list.size() == 1);
    CHECK(list[0].body == "你好");
    CHECK(list[0].status == "pending");
    CHECK(h.list("p2").empty());

    h.set_status(m.id, "ok");
    CHECK(h.get(m.id).status == "ok");
    h.set_file_path(m.id, "");
    CHECK(h.get(m.id).file_path == "");
    CHECK(h.get(99999).id == 0);
  }
  fs::remove_all(dir);
}

TEST_CASE("History 文件消息字段完整保存") {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / ("dd_db2_" + util::gen_id());
  fs::create_directories(dir);
  {
    History h(dir / "t.db");
    Message m;
    m.peer_id = "p1";
    m.direction = "in";
    m.kind = "file";
    m.file_name = "照片.jpg";
    m.file_size = 12345;
    m.file_path = "/tmp/照片.jpg";
    m.status = "ok";
    h.add(m);
    auto got = h.get(m.id);
    CHECK(got.file_name == "照片.jpg");
    CHECK(got.file_size == 12345);
    CHECK(got.file_path == "/tmp/照片.jpg");
  }
  fs::remove_all(dir);
}
```

- [ ] **Step 2: src/history.h**

```cpp
#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "json.hpp"

struct sqlite3;

struct Message {
  long long id = 0;
  std::string peer_id;
  std::string direction;  // "in" | "out"
  std::string kind;       // "text" | "file"
  std::string body;       // 文本内容（文件消息为空）
  std::string file_name;
  long long file_size = 0;
  std::string file_path;  // 收到的保存路径 / 发送暂存路径
  std::string status;     // "ok" | "fail" | "pending"
  long long ts = 0;       // 毫秒时间戳
};

nlohmann::json to_json(const Message& m);

class History {
 public:
  explicit History(const std::filesystem::path& db_file);
  ~History();
  History(const History&) = delete;
  History& operator=(const History&) = delete;

  long long add(Message& m);  // 回填 m.id；m.ts 为 0 时回填当前时间
  std::vector<Message> list(const std::string& peer_id, int limit = 200);  // 按时间升序
  void set_status(long long id, const std::string& status);
  void set_file_path(long long id, const std::string& path);
  Message get(long long id);  // 不存在时返回 id==0 的空消息

 private:
  sqlite3* db_ = nullptr;
};
```

- [ ] **Step 3: 运行确认链接失败**

```bash
cmake -B build && cmake --build build -j 2>&1 | tail -5
```

预期：`undefined reference to History::History` 等

- [ ] **Step 4: 实现 src/history.cpp**

```cpp
#include "history.h"
#include <sqlite3.h>
#include <algorithm>
#include <stdexcept>
#include "util.h"

nlohmann::json to_json(const Message& m) {
  return {{"id", m.id},           {"peer_id", m.peer_id},
          {"direction", m.direction}, {"kind", m.kind},
          {"body", m.body},       {"file_name", m.file_name},
          {"file_size", m.file_size}, {"file_path", m.file_path},
          {"status", m.status},   {"ts", m.ts}};
}

static void exec_sql(sqlite3* db, const char* sql) {
  char* err = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    std::string e = err ? err : "sqlite 错误";
    sqlite3_free(err);
    throw std::runtime_error(e);
  }
}

History::History(const std::filesystem::path& db_file) {
  if (sqlite3_open(db_file.u8string().c_str(), &db_) != SQLITE_OK)
    throw std::runtime_error("无法打开数据库: " + db_file.string());
  exec_sql(db_, "PRAGMA journal_mode=WAL;");
  exec_sql(db_, R"sql(
CREATE TABLE IF NOT EXISTS messages(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  peer_id TEXT NOT NULL,
  direction TEXT NOT NULL,
  kind TEXT NOT NULL,
  body TEXT NOT NULL DEFAULT '',
  file_name TEXT NOT NULL DEFAULT '',
  file_size INTEGER NOT NULL DEFAULT 0,
  file_path TEXT NOT NULL DEFAULT '',
  status TEXT NOT NULL DEFAULT 'ok',
  ts INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_peer ON messages(peer_id, id);
)sql");
}

History::~History() {
  if (db_) sqlite3_close(db_);
}

static Message row_to_message(sqlite3_stmt* st) {
  auto txt = [&](int col) {
    const unsigned char* t = sqlite3_column_text(st, col);
    return t ? std::string((const char*)t) : std::string();
  };
  Message m;
  m.id = sqlite3_column_int64(st, 0);
  m.peer_id = txt(1);
  m.direction = txt(2);
  m.kind = txt(3);
  m.body = txt(4);
  m.file_name = txt(5);
  m.file_size = sqlite3_column_int64(st, 6);
  m.file_path = txt(7);
  m.status = txt(8);
  m.ts = sqlite3_column_int64(st, 9);
  return m;
}

long long History::add(Message& m) {
  if (m.ts == 0) m.ts = util::now_ms();
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_,
                     "INSERT INTO messages(peer_id,direction,kind,body,file_name,"
                     "file_size,file_path,status,ts) VALUES(?,?,?,?,?,?,?,?,?)",
                     -1, &st, nullptr);
  sqlite3_bind_text(st, 1, m.peer_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, m.direction.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, m.kind.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, m.body.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 5, m.file_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 6, m.file_size);
  sqlite3_bind_text(st, 7, m.file_path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 8, m.status.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 9, m.ts);
  sqlite3_step(st);
  sqlite3_finalize(st);
  m.id = sqlite3_last_insert_rowid(db_);
  return m.id;
}

std::vector<Message> History::list(const std::string& peer_id, int limit) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_,
                     "SELECT id,peer_id,direction,kind,body,file_name,file_size,"
                     "file_path,status,ts FROM messages WHERE peer_id=? "
                     "ORDER BY id DESC LIMIT ?",
                     -1, &st, nullptr);
  sqlite3_bind_text(st, 1, peer_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 2, limit);
  std::vector<Message> v;
  while (sqlite3_step(st) == SQLITE_ROW) v.push_back(row_to_message(st));
  sqlite3_finalize(st);
  std::reverse(v.begin(), v.end());  // 升序返回
  return v;
}

void History::set_status(long long id, const std::string& status) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_, "UPDATE messages SET status=? WHERE id=?", -1, &st, nullptr);
  sqlite3_bind_text(st, 1, status.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 2, id);
  sqlite3_step(st);
  sqlite3_finalize(st);
}

void History::set_file_path(long long id, const std::string& path) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_, "UPDATE messages SET file_path=? WHERE id=?", -1, &st, nullptr);
  sqlite3_bind_text(st, 1, path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 2, id);
  sqlite3_step(st);
  sqlite3_finalize(st);
}

Message History::get(long long id) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_,
                     "SELECT id,peer_id,direction,kind,body,file_name,file_size,"
                     "file_path,status,ts FROM messages WHERE id=?",
                     -1, &st, nullptr);
  sqlite3_bind_int64(st, 1, id);
  Message m;
  if (sqlite3_step(st) == SQLITE_ROW) m = row_to_message(st);
  sqlite3_finalize(st);
  return m;
}
```

- [ ] **Step 5: 运行测试确认通过**

```bash
cmake -B build && cmake --build build -j && ./build/unit_tests
```

预期：全部 PASS

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: SQLite 消息历史模块"
```

---

### Task 7: EventBus（SSE 事件总线，TDD）

**Files:**
- Create: `src/events.h`, `src/events.cpp`, `tests/test_events.cpp`

- [ ] **Step 1: 写失败的测试 tests/test_events.cpp**

```cpp
#include "doctest.h"
#include "events.h"

TEST_CASE("EventBus 发布到所有订阅者，退订后不再接收") {
  EventBus bus;
  auto a = bus.subscribe();
  auto b = bus.subscribe();
  bus.publish("hello");
  {
    std::lock_guard<std::mutex> lk(a->mu);
    REQUIRE(a->queue.size() == 1);
    CHECK(a->queue.front() == "hello");
  }
  bus.unsubscribe(a);
  bus.publish("again");
  {
    std::lock_guard<std::mutex> lk(a->mu);
    CHECK(a->queue.size() == 1);  // 没有新增
  }
  {
    std::lock_guard<std::mutex> lk(b->mu);
    CHECK(b->queue.size() == 2);
  }
}
```

- [ ] **Step 2: src/events.h**

```cpp
#pragma once
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// 简单的发布-订阅总线：HTTP SSE 处理器订阅，业务代码发布 JSON 字符串
class EventBus {
 public:
  struct Subscriber {
    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::string> queue;
  };
  using SubPtr = std::shared_ptr<Subscriber>;

  SubPtr subscribe();
  void unsubscribe(const SubPtr& s);
  void publish(const std::string& payload);

 private:
  std::mutex mu_;
  std::vector<SubPtr> subs_;
};
```

- [ ] **Step 3: 运行确认链接失败，然后实现 src/events.cpp**

```cpp
#include "events.h"
#include <algorithm>

EventBus::SubPtr EventBus::subscribe() {
  auto s = std::make_shared<Subscriber>();
  std::lock_guard<std::mutex> lk(mu_);
  subs_.push_back(s);
  return s;
}

void EventBus::unsubscribe(const SubPtr& s) {
  std::lock_guard<std::mutex> lk(mu_);
  subs_.erase(std::remove(subs_.begin(), subs_.end(), s), subs_.end());
}

void EventBus::publish(const std::string& payload) {
  std::lock_guard<std::mutex> lk(mu_);
  for (auto& s : subs_) {
    std::lock_guard<std::mutex> lk2(s->mu);
    s->queue.push_back(payload);
    s->cv.notify_one();
  }
}
```

- [ ] **Step 4: 运行测试确认通过**

```bash
cmake -B build && cmake --build build -j && ./build/unit_tests
```

预期：全部 PASS

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: SSE 事件总线"
```

---

### Task 8: App 骨架（HTTP 服务 + 发现接口 + SSE + main）

**Files:**
- Create: `src/server.h`, `src/server.cpp`
- Modify: `src/main.cpp`（整体替换）
- Create: `web/index.html`（临时占位，任务 13 替换）

- [ ] **Step 1: src/server.h（最终版头文件，方法逐任务补实现）**

```cpp
#pragma once
#include <filesystem>
#include <memory>
#include "config.h"
#include "discovery.h"
#include "events.h"
#include "history.h"
#include "httplib.h"

class App {
 public:
  App(Config cfg, std::filesystem::path web_dir);
  bool run();  // 阻塞运行；绑定失败返回 false
  int port() const { return port_; }

 private:
  void setup_routes();
  void publish_message(const char* type, const Message& m);
  void send_text(const PeerInfo& peer, const Message& m);  // 同步，更新状态
  void start_file_send(long long msg_id);                  // 后台线程推送文件

  Config cfg_;
  std::filesystem::path web_dir_;
  History history_;
  EventBus bus_;
  std::unique_ptr<Discovery> discovery_;
  httplib::Server svr_;
  int port_ = 0;
};
```

- [ ] **Step 2: src/server.cpp（本任务实现：构造、run、静态页、/api/self、/api/peers、/api/events）**

```cpp
#include "server.h"
#include <cstdio>
#include "json.hpp"
#include "util.h"

using nlohmann::json;

App::App(Config cfg, std::filesystem::path web_dir)
    : cfg_(std::move(cfg)),
      web_dir_(std::move(web_dir)),
      history_(cfg_.db_path()) {}

bool App::run() {
  namespace fs = std::filesystem;
  fs::create_directories(cfg_.download_dir);
  fs::create_directories(cfg_.staging_dir());

  port_ = cfg_.port;
  while (port_ < cfg_.port + 10 && !svr_.bind_to_port("0.0.0.0", port_)) port_++;
  if (port_ >= cfg_.port + 10) {
    std::printf("错误：端口 %d-%d 都被占用\n", cfg_.port, port_ - 1);
    return false;
  }

  discovery_ = std::make_unique<Discovery>(cfg_.id, cfg_.name, port_);
  discovery_->start([this] { bus_.publish(json{{"type", "peers"}}.dump()); });

  setup_routes();
  std::printf("已启动：http://localhost:%d  （设备名：%s）\n", port_, cfg_.name.c_str());
  return svr_.listen_after_bind();
}

void App::publish_message(const char* type, const Message& m) {
  bus_.publish(json{{"type", type}, {"message", to_json(m)}}.dump());
}

void App::setup_routes() {
  svr_.set_mount_point("/", web_dir_.string());

  svr_.Get("/api/self", [this](const httplib::Request&, httplib::Response& res) {
    json j{{"id", cfg_.id}, {"name", cfg_.name}, {"port", port_}};
    res.set_content(j.dump(), "application/json");
  });

  svr_.Get("/api/peers", [this](const httplib::Request&, httplib::Response& res) {
    json arr = json::array();
    long long now = util::now_ms();
    for (auto& p : discovery_->peers()) {
      arr.push_back({{"id", p.id},
                     {"name", p.name},
                     {"ip", p.ip},
                     {"port", p.port},
                     {"online", now - p.last_seen < discovery::kOfflineMs}});
    }
    res.set_content(arr.dump(), "application/json");
  });

  svr_.Get("/api/events", [this](const httplib::Request&, httplib::Response& res) {
    auto sub = bus_.subscribe();
    res.set_header("Cache-Control", "no-cache");
    res.set_chunked_content_provider(
        "text/event-stream",
        [sub](size_t, httplib::DataSink& sink) {
          std::string chunk;
          {
            std::unique_lock<std::mutex> lk(sub->mu);
            sub->cv.wait_for(lk, std::chrono::seconds(5),
                             [&] { return !sub->queue.empty(); });
            if (sub->queue.empty()) {
              chunk = ": ping\n\n";  // 保活注释行
            } else {
              while (!sub->queue.empty()) {
                chunk += "data: " + sub->queue.front() + "\n\n";
                sub->queue.pop_front();
              }
            }
          }
          return sink.write(chunk.data(), chunk.size());
        },
        [this, sub](bool) { bus_.unsubscribe(sub); });
  });
}

void App::send_text(const PeerInfo&, const Message&) {}  // 任务 9 实现
void App::start_file_send(long long) {}                  // 任务 11 实现
```

- [ ] **Step 3: 替换 src/main.cpp**

```cpp
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include "config.h"
#include "server.h"
#include "util.h"

int main(int argc, char** argv) {
  namespace fs = std::filesystem;
  fs::path data_dir = util::home_dir() / ".duoduan";
  int port_override = 0;
  std::string name_override, download_override;
  for (int i = 1; i + 1 < argc; ++i) {
    if (!std::strcmp(argv[i], "--data-dir")) data_dir = argv[i + 1];
    if (!std::strcmp(argv[i], "--port")) port_override = std::atoi(argv[i + 1]);
    if (!std::strcmp(argv[i], "--name")) name_override = argv[i + 1];
    if (!std::strcmp(argv[i], "--download-dir")) download_override = argv[i + 1];
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

- [ ] **Step 4: 临时占位 web/index.html**

```html
<!DOCTYPE html>
<html lang="zh"><head><meta charset="utf-8"><title>多端互通</title></head>
<body>界面开发中（任务 13 替换）</body></html>
```

- [ ] **Step 5: 构建 + 双实例集成验证**

```bash
cmake -B build && cmake --build build -j
./build/duoduan --data-dir /tmp/dd_a --port 8520 --name 甲 &
./build/duoduan --data-dir /tmp/dd_b --port 8530 --name 乙 &
sleep 4
curl -s http://localhost:8520/api/self
echo
curl -s http://localhost:8520/api/peers
echo
curl -s http://localhost:8530/api/peers
echo
```

预期：
- `/api/self` 返回 `{"id":"…","name":"甲","port":8520}`
- 8520 的 `/api/peers` 包含 `"name":"乙","online":true`
- 8530 的 `/api/peers` 包含 `"name":"甲","online":true`

- [ ] **Step 6: SSE 验证**

```bash
timeout 6 curl -sN http://localhost:8520/api/events; echo
```

预期：6 秒内至少输出一次 `: ping`（若期间乙重启则会输出 `data: {"type":"peers"}`）

- [ ] **Step 7: 静态页验证 + 清理后台进程**

```bash
curl -s http://localhost:8520/ | head -2
kill %1 %2
```

预期：输出占位 HTML 第一行 `<!DOCTYPE html>`

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "feat: HTTP 服务骨架（设备发现接口 + SSE + 静态页）"
```

---

### Task 9: 文字消息收发

**Files:**
- Modify: `src/server.cpp`（setup_routes 末尾追加路由；替换 send_text 空实现）

- [ ] **Step 1: 在 setup_routes() 末尾追加三个路由**

```cpp
  // 历史记录
  svr_.Get("/api/messages", [this](const httplib::Request& req, httplib::Response& res) {
    auto peer = req.get_param_value("peer");
    json arr = json::array();
    for (auto& m : history_.list(peer)) arr.push_back(to_json(m));
    res.set_content(arr.dump(), "application/json");
  });

  // 本机浏览器 → 发送文字
  svr_.Post("/api/send-text", [this](const httplib::Request& req, httplib::Response& res) {
    auto j = json::parse(req.body, nullptr, false);
    if (j.is_discarded()) { res.status = 400; return; }
    std::string peer_id = j.value("peer_id", "");
    std::string text = j.value("text", "");
    PeerInfo peer;
    if (text.empty() || !discovery_->find(peer_id, peer)) { res.status = 400; return; }
    Message m;
    m.peer_id = peer_id;
    m.direction = "out";
    m.kind = "text";
    m.body = text;
    m.status = "pending";
    history_.add(m);
    send_text(peer, m);
    m = history_.get(m.id);
    publish_message("message", m);
    res.set_content(to_json(m).dump(), "application/json");
  });

  // 其他电脑 → 接收文字
  svr_.Post("/peer/text", [this](const httplib::Request& req, httplib::Response& res) {
    auto j = json::parse(req.body, nullptr, false);
    if (j.is_discarded()) { res.status = 400; return; }
    Message m;
    m.peer_id = j.value("from_id", "");
    m.direction = "in";
    m.kind = "text";
    m.body = j.value("text", "");
    m.status = "ok";
    if (m.peer_id.empty() || m.body.empty()) { res.status = 400; return; }
    history_.add(m);
    publish_message("message", m);
    res.set_content("{}", "application/json");
  });
```

- [ ] **Step 2: 替换 send_text 的空实现**

删除 `void App::send_text(const PeerInfo&, const Message&) {}`，改为：

```cpp
void App::send_text(const PeerInfo& peer, const Message& m) {
  httplib::Client cli(peer.ip, peer.port);
  cli.set_connection_timeout(3);
  json j{{"from_id", cfg_.id}, {"from_name", cfg_.name}, {"text", m.body}};
  auto r = cli.Post("/peer/text", j.dump(), "application/json");
  history_.set_status(m.id, (r && r->status == 200) ? "ok" : "fail");
}
```

- [ ] **Step 3: 构建 + 双实例验证**

```bash
cmake -B build && cmake --build build -j
rm -rf /tmp/dd_a /tmp/dd_b
./build/duoduan --data-dir /tmp/dd_a --port 8520 --name 甲 &
./build/duoduan --data-dir /tmp/dd_b --port 8530 --name 乙 &
sleep 4
B_ID=$(curl -s http://localhost:8530/api/self | python3 -c 'import sys,json;print(json.load(sys.stdin)["id"])')
A_ID=$(curl -s http://localhost:8520/api/self | python3 -c 'import sys,json;print(json.load(sys.stdin)["id"])')
curl -s -X POST http://localhost:8520/api/send-text \
  -H 'Content-Type: application/json' \
  -d "{\"peer_id\":\"$B_ID\",\"text\":\"你好，乙\"}"
echo
curl -s "http://localhost:8530/api/messages?peer=$A_ID"
echo
kill %1 %2
```

预期：
- send-text 返回 `"status":"ok"`、`"direction":"out"`、`"body":"你好，乙"`
- 乙的 messages 返回数组，含 `"direction":"in"`、`"body":"你好，乙"`

- [ ] **Step 4: 失败路径验证（对方不在线）**

```bash
rm -rf /tmp/dd_a
./build/duoduan --data-dir /tmp/dd_a --port 8520 --name 甲 &
sleep 1
curl -s -X POST http://localhost:8520/api/send-text \
  -H 'Content-Type: application/json' \
  -d '{"peer_id":"不存在的id","text":"hi"}' -o /dev/null -w '%{http_code}\n'
kill %1
```

预期：`400`

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: 文字消息收发（含历史记录与 SSE 通知）"
```

---

### Task 10: 文件接收（/peer/file）

**Files:**
- Modify: `src/server.cpp`（setup_routes 末尾追加路由）

- [ ] **Step 1: setup_routes() 末尾追加流式文件接收路由**

```cpp
  // 其他电脑 → 接收文件（流式写 .part，完成后改名）
  svr_.Post("/peer/file", [this](const httplib::Request& req, httplib::Response& res,
                                 const httplib::ContentReader& reader) {
    namespace fs = std::filesystem;
    std::string name = req.get_param_value("name");
    std::string from_id = req.get_param_value("from_id");
    long long size = std::atoll(req.get_param_value("size").c_str());
    if (name.empty() || from_id.empty()) { res.status = 400; return; }
    // 文件名只取末段，防止路径穿越
    name = fs::path(name).filename().string();
    fs::create_directories(cfg_.download_dir);
    fs::path part = util::unique_path(cfg_.download_dir, name + ".part");
    std::ofstream ofs(part, std::ios::binary);
    reader([&](const char* data, size_t len) {
      ofs.write(data, (std::streamsize)len);
      return ofs.good();
    });
    ofs.close();
    std::error_code ec;
    bool ok = fs::exists(part) && (size <= 0 || (long long)fs::file_size(part, ec) == size);
    if (!ok) {
      fs::remove(part, ec);
      res.status = 500;
      return;
    }
    fs::path final_path = util::unique_path(cfg_.download_dir, name);
    fs::rename(part, final_path, ec);
    Message m;
    m.peer_id = from_id;
    m.direction = "in";
    m.kind = "file";
    m.file_name = final_path.filename().string();
    m.file_size = (long long)fs::file_size(final_path, ec);
    m.file_path = final_path.string();
    m.status = "ok";
    history_.add(m);
    publish_message("message", m);
    res.set_content("{}", "application/json");
  });
```

需要在 server.cpp 顶部补充 `#include <fstream>`。

- [ ] **Step 2: 构建 + curl 直接投递文件验证**

```bash
cmake -B build && cmake --build build -j
rm -rf /tmp/dd_b /tmp/dd_b_dl
./build/duoduan --data-dir /tmp/dd_b --port 8530 --name 乙 --download-dir /tmp/dd_b_dl &
sleep 1
head -c 1000000 /dev/urandom > /tmp/test_send.bin
SIZE=$(stat -c%s /tmp/test_send.bin)
curl -s -X POST "http://localhost:8530/peer/file?name=测试.bin&from_id=fake01&from_name=甲&size=$SIZE" \
  --data-binary @/tmp/test_send.bin -H 'Content-Type: application/octet-stream'
echo
ls -la /tmp/dd_b_dl/
cmp /tmp/test_send.bin "/tmp/dd_b_dl/测试.bin" && echo "内容一致"
curl -s "http://localhost:8530/api/messages?peer=fake01"
echo
```

预期：
- 返回 `{}`
- 下载目录出现 `测试.bin`，`cmp` 输出 `内容一致`
- messages 含 `"kind":"file"`、`"file_name":"测试.bin"`、`"status":"ok"`

- [ ] **Step 3: 同名文件去重验证**

```bash
curl -s -X POST "http://localhost:8530/peer/file?name=测试.bin&from_id=fake01&from_name=甲&size=$SIZE" \
  --data-binary @/tmp/test_send.bin -H 'Content-Type: application/octet-stream'
ls /tmp/dd_b_dl/
kill %1
```

预期：出现 `测试 (1).bin`

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat: 文件流式接收（.part 临时文件 + 同名去重）"
```

---

### Task 11: 文件发送（暂存上传 + 后台推送 + 进度）

**Files:**
- Modify: `src/server.cpp`（追加 /api/send-file 路由；替换 start_file_send 空实现）

- [ ] **Step 1: setup_routes() 末尾追加浏览器上传路由（流式 multipart）**

```cpp
  // 本机浏览器 → 上传文件到暂存区并后台推送给目标设备
  svr_.Post("/api/send-file", [this](const httplib::Request& req, httplib::Response& res,
                                     const httplib::ContentReader& reader) {
    namespace fs = std::filesystem;
    std::string peer_id = req.get_param_value("peer");
    PeerInfo peer;
    if (!discovery_->find(peer_id, peer)) { res.status = 400; return; }
    fs::create_directories(cfg_.staging_dir());
    std::string filename;
    fs::path staged;
    auto ofs = std::make_shared<std::ofstream>();
    reader(
        [&](const httplib::MultipartFormData& file) {
          filename = fs::path(file.filename).filename().string();
          if (filename.empty()) filename = "未命名";
          staged = util::unique_path(cfg_.staging_dir(), filename);
          ofs->open(staged, std::ios::binary);
          return ofs->good();
        },
        [&](const char* data, size_t len) {
          ofs->write(data, (std::streamsize)len);
          return ofs->good();
        });
    ofs->close();
    std::error_code ec;
    if (filename.empty() || !fs::exists(staged)) { res.status = 400; return; }
    Message m;
    m.peer_id = peer_id;
    m.direction = "out";
    m.kind = "file";
    m.file_name = filename;
    m.file_size = (long long)fs::file_size(staged, ec);
    m.file_path = staged.string();
    m.status = "pending";
    history_.add(m);
    publish_message("message", m);
    start_file_send(m.id);
    res.set_content(to_json(m).dump(), "application/json");
  });
```

- [ ] **Step 2: 替换 start_file_send 的空实现**

删除 `void App::start_file_send(long long) {}`，改为：

```cpp
void App::start_file_send(long long msg_id) {
  std::thread([this, msg_id] {
    namespace fs = std::filesystem;
    Message m = history_.get(msg_id);
    PeerInfo peer;
    bool ok = false;
    if (m.id != 0 && discovery_->find(m.peer_id, peer) && fs::exists(m.file_path)) {
      httplib::Client cli(peer.ip, peer.port);
      cli.set_connection_timeout(3);
      cli.set_read_timeout(60);
      cli.set_write_timeout(30);
      auto file = std::make_shared<std::ifstream>(m.file_path, std::ios::binary);
      long long total = m.file_size;
      auto sent = std::make_shared<long long>(0);
      auto last_pub = std::make_shared<long long>(0);
      std::string path = "/peer/file?name=" + util::url_encode(m.file_name) +
                         "&from_id=" + util::url_encode(cfg_.id) +
                         "&from_name=" + util::url_encode(cfg_.name) +
                         "&size=" + std::to_string(total);
      auto r = cli.Post(
          path, httplib::Headers{}, (size_t)total,
          [this, file, sent, last_pub, total, msg_id](size_t, size_t length,
                                                      httplib::DataSink& sink) {
            char buf[65536];
            size_t want = std::min(length, sizeof(buf));
            file->read(buf, (std::streamsize)want);
            std::streamsize n = file->gcount();
            if (n <= 0) return false;
            if (!sink.write(buf, (size_t)n)) return false;
            *sent += n;
            long long now = util::now_ms();
            if (now - *last_pub > 200 || *sent >= total) {
              *last_pub = now;
              bus_.publish(json{{"type", "progress"},
                                {"message_id", msg_id},
                                {"sent", *sent},
                                {"total", total}}.dump());
            }
            return true;
          },
          "application/octet-stream");
      ok = r && r->status == 200;
    }
    history_.set_status(msg_id, ok ? "ok" : "fail");
    if (ok) {
      std::error_code ec;
      fs::remove(m.file_path, ec);          // 发送成功删除暂存
      history_.set_file_path(msg_id, "");   // 历史只留文件名和大小
    }
    publish_message("message_update", history_.get(msg_id));
  }).detach();
}
```

- [ ] **Step 3: 构建 + 双实例端到端验证**

```bash
cmake -B build && cmake --build build -j
rm -rf /tmp/dd_a /tmp/dd_b /tmp/dd_b_dl
./build/duoduan --data-dir /tmp/dd_a --port 8520 --name 甲 &
./build/duoduan --data-dir /tmp/dd_b --port 8530 --name 乙 --download-dir /tmp/dd_b_dl &
sleep 4
B_ID=$(curl -s http://localhost:8530/api/self | python3 -c 'import sys,json;print(json.load(sys.stdin)["id"])')
head -c 5000000 /dev/urandom > /tmp/big.bin
curl -s -X POST "http://localhost:8520/api/send-file?peer=$B_ID" -F "file=@/tmp/big.bin"
echo
sleep 3
ls -la /tmp/dd_b_dl/
cmp /tmp/big.bin /tmp/dd_b_dl/big.bin && echo "内容一致"
ls /tmp/dd_a/staging/   # 应为空：发送成功后暂存被删除
curl -s "http://localhost:8520/api/messages?peer=$B_ID" | python3 -m json.tool | grep -E 'status|file_name'
kill %1 %2
```

预期：
- send-file 立即返回 `"status":"pending"`
- 3 秒后乙的下载目录出现 `big.bin`，`内容一致`
- 甲的 staging 目录为空
- 甲的历史记录该消息 `"status": "ok"`

- [ ] **Step 4: 进度事件验证**

```bash
rm -rf /tmp/dd_a /tmp/dd_b /tmp/dd_b_dl
./build/duoduan --data-dir /tmp/dd_a --port 8520 --name 甲 &
./build/duoduan --data-dir /tmp/dd_b --port 8530 --name 乙 --download-dir /tmp/dd_b_dl &
sleep 4
B_ID=$(curl -s http://localhost:8530/api/self | python3 -c 'import sys,json;print(json.load(sys.stdin)["id"])')
timeout 5 curl -sN http://localhost:8520/api/events > /tmp/events.log &
head -c 50000000 /dev/urandom > /tmp/big50.bin
curl -s -X POST "http://localhost:8520/api/send-file?peer=$B_ID" -F "file=@/tmp/big50.bin" > /dev/null
sleep 5
grep -c progress /tmp/events.log
kill %1 %2
```

预期：`grep -c` 输出 ≥ 1（收到 progress 事件）

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: 文件发送（暂存+后台推送+SSE进度）"
```

---

### Task 12: 发送失败重试

**Files:**
- Modify: `src/server.cpp`（setup_routes 末尾追加 /api/retry）

- [ ] **Step 1: setup_routes() 末尾追加重试路由**

```cpp
  // 重试发送失败的消息
  svr_.Post("/api/retry", [this](const httplib::Request& req, httplib::Response& res) {
    auto j = json::parse(req.body, nullptr, false);
    long long id = j.is_discarded() ? 0 : j.value("message_id", (long long)0);
    Message m = history_.get(id);
    if (m.id == 0 || m.direction != "out" || m.status != "fail") {
      res.status = 400;
      return;
    }
    history_.set_status(m.id, "pending");
    publish_message("message_update", history_.get(m.id));
    if (m.kind == "text") {
      PeerInfo peer;
      if (discovery_->find(m.peer_id, peer)) {
        send_text(peer, m);
      } else {
        history_.set_status(m.id, "fail");
      }
      publish_message("message_update", history_.get(m.id));
    } else {
      start_file_send(m.id);  // 文件：暂存还在（失败时不删除）
    }
    res.set_content("{}", "application/json");
  });
```

- [ ] **Step 2: 构建 + 失败→重试验证**

```bash
cmake -B build && cmake --build build -j
rm -rf /tmp/dd_a /tmp/dd_b
./build/duoduan --data-dir /tmp/dd_a --port 8520 --name 甲 &
./build/duoduan --data-dir /tmp/dd_b --port 8530 --name 乙 &
sleep 4
B_ID=$(curl -s http://localhost:8530/api/self | python3 -c 'import sys,json;print(json.load(sys.stdin)["id"])')
kill %2 && sleep 1   # 停掉乙，制造失败
MSG=$(curl -s -X POST http://localhost:8520/api/send-text \
  -H 'Content-Type: application/json' \
  -d "{\"peer_id\":\"$B_ID\",\"text\":\"重试测试\"}")
echo "$MSG"   # 预期 status: fail
MSG_ID=$(echo "$MSG" | python3 -c 'import sys,json;print(json.load(sys.stdin)["id"])')
./build/duoduan --data-dir /tmp/dd_b --port 8530 --name 乙 &   # 重启乙
sleep 4
curl -s -X POST http://localhost:8520/api/retry \
  -H 'Content-Type: application/json' -d "{\"message_id\":$MSG_ID}"
sleep 1
A_ID=$(curl -s http://localhost:8520/api/self | python3 -c 'import sys,json;print(json.load(sys.stdin)["id"])')
curl -s "http://localhost:8530/api/messages?peer=$A_ID" | grep -o '重试测试'
curl -s "http://localhost:8520/api/messages?peer=$B_ID" | python3 -m json.tool | grep status
kill %1 %2
```

预期：
- 第一次发送返回 `"status":"fail"`
- 重试后乙收到 `重试测试`，甲侧该消息 `"status": "ok"`

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "feat: 失败消息重试"
```

---

### Task 13: 前端聊天界面

**Files:**
- Modify: `web/index.html`（整体替换）
- Create: `web/style.css`, `web/app.js`

- [ ] **Step 1: web/index.html（整体替换占位文件）**

```html
<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>多端互通</title>
<link rel="stylesheet" href="style.css">
</head>
<body>
<div id="sidebar">
  <div id="self-name">…</div>
  <div id="peer-list"></div>
</div>
<div id="main">
  <div id="chat-header">选择左侧设备开始传输</div>
  <div id="messages"></div>
  <div id="composer" hidden>
    <button id="file-btn" title="发送文件">📎</button>
    <input id="file-input" type="file" multiple hidden>
    <textarea id="text-input" rows="2" placeholder="输入消息，Enter 发送；拖文件到聊天区可直接发送"></textarea>
    <button id="send-btn">发送</button>
  </div>
</div>
<script src="app.js"></script>
</body>
</html>
```

- [ ] **Step 2: web/style.css**

```css
* { margin: 0; padding: 0; box-sizing: border-box; }
html, body { height: 100%; font-family: system-ui, "Microsoft YaHei", sans-serif; }
body { display: flex; background: #f5f5f5; }

#sidebar { width: 240px; background: #2e2e2e; color: #ddd; display: flex; flex-direction: column; }
#self-name { padding: 16px 14px; font-weight: bold; border-bottom: 1px solid #444; color: #fff; }
#peer-list { flex: 1; overflow-y: auto; }
.peer { padding: 14px; cursor: pointer; display: flex; align-items: center; gap: 8px; }
.peer:hover { background: #3a3a3a; }
.peer.active { background: #4a4a4a; }
.peer .dot { width: 9px; height: 9px; border-radius: 50%; background: #4caf50; flex: none; }
.peer.offline .dot { background: #888; }
.peer.offline .pname { color: #888; }
.empty { padding: 20px 14px; color: #888; font-size: 13px; }

#main { flex: 1; display: flex; flex-direction: column; min-width: 0; }
#chat-header { padding: 14px 20px; background: #fff; border-bottom: 1px solid #e0e0e0; font-weight: bold; }
#messages { flex: 1; overflow-y: auto; padding: 16px 20px; }

.msg { display: flex; flex-direction: column; margin-bottom: 14px; align-items: flex-start; }
.msg.out { align-items: flex-end; }
.bubble { max-width: 65%; padding: 10px 13px; border-radius: 8px; background: #fff;
          box-shadow: 0 1px 2px rgba(0,0,0,.08); white-space: pre-wrap; word-break: break-all; }
.msg.out .bubble { background: #95ec69; }
.msg.fail .bubble { outline: 1px solid #e53935; }
.meta { font-size: 11px; color: #999; margin-top: 4px; }
.meta a { color: #e53935; }
.fname { font-weight: bold; }
.fmeta { font-size: 12px; color: #777; margin-top: 4px; }
.bar { height: 4px; background: #ddd; border-radius: 2px; margin-top: 6px; overflow: hidden; }
.bar i { display: block; height: 100%; width: 0; background: #4caf50; transition: width .2s; }

#composer { display: flex; gap: 8px; padding: 12px 20px; background: #fff; border-top: 1px solid #e0e0e0; align-items: flex-end; }
#text-input { flex: 1; resize: none; border: 1px solid #ccc; border-radius: 6px; padding: 8px; font: inherit; }
#composer button { border: none; border-radius: 6px; padding: 9px 16px; cursor: pointer; font: inherit; }
#send-btn { background: #07c160; color: #fff; }
#file-btn { background: #eee; }
```

- [ ] **Step 3: web/app.js**

```js
const $ = (s) => document.querySelector(s);
let self = null, peers = [], current = null, messages = [];

async function api(path, opts) {
  const r = await fetch(path, opts);
  if (!r.ok) throw new Error('请求失败: ' + r.status);
  return r.json();
}

function fmtSize(n) {
  if (n < 1024) return n + ' B';
  if (n < 1048576) return (n / 1024).toFixed(1) + ' KB';
  if (n < 1073741824) return (n / 1048576).toFixed(1) + ' MB';
  return (n / 1073741824).toFixed(2) + ' GB';
}

function fmtTime(ts) { return new Date(ts).toLocaleString('zh-CN'); }

async function refreshPeers() {
  peers = await api('/api/peers');
  renderPeers();
}

function renderPeers() {
  const el = $('#peer-list');
  el.innerHTML = '';
  for (const p of peers) {
    const d = document.createElement('div');
    d.className = 'peer' + (current === p.id ? ' active' : '') + (p.online ? '' : ' offline');
    const dot = document.createElement('span');
    dot.className = 'dot';
    const name = document.createElement('span');
    name.className = 'pname';
    name.textContent = p.name;
    d.append(dot, name);
    d.onclick = () => selectPeer(p.id);
    el.appendChild(d);
  }
  if (!peers.length) {
    el.innerHTML = '<div class="empty">正在搜索局域网设备…<br>请确认对方已启动本程序</div>';
  }
}

async function selectPeer(id) {
  current = id;
  const p = peers.find((x) => x.id === id);
  $('#chat-header').textContent = p ? p.name : '';
  $('#composer').hidden = false;
  messages = await api('/api/messages?peer=' + encodeURIComponent(id));
  renderMessages();
  renderPeers();
}

function msgEl(m) {
  const d = document.createElement('div');
  d.className = 'msg ' + m.direction + (m.status === 'fail' ? ' fail' : '');
  d.dataset.id = m.id;
  const b = document.createElement('div');
  b.className = 'bubble';
  if (m.kind === 'text') {
    b.textContent = m.body;
  } else {
    const fname = document.createElement('div');
    fname.className = 'fname';
    fname.textContent = '📄 ' + m.file_name;
    const fmeta = document.createElement('div');
    fmeta.className = 'fmeta';
    fmeta.textContent = fmtSize(m.file_size) +
      (m.direction === 'in' && m.file_path ? ' · 已保存到 ' + m.file_path : '');
    b.append(fname, fmeta);
    if (m.status === 'pending') {
      const bar = document.createElement('div');
      bar.className = 'bar';
      bar.appendChild(document.createElement('i'));
      b.appendChild(bar);
    }
  }
  d.appendChild(b);
  const meta = document.createElement('div');
  meta.className = 'meta';
  meta.textContent = fmtTime(m.ts) +
    (m.status === 'fail' ? ' · 发送失败' : m.status === 'pending' ? ' · 发送中…' : '');
  if (m.status === 'fail' && m.direction === 'out') {
    const r = document.createElement('a');
    r.href = '#';
    r.textContent = ' 重试';
    r.onclick = (e) => {
      e.preventDefault();
      api('/api/retry', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ message_id: m.id }),
      });
    };
    meta.appendChild(r);
  }
  d.appendChild(meta);
  return d;
}

function renderMessages() {
  const box = $('#messages');
  box.innerHTML = '';
  for (const m of messages) box.appendChild(msgEl(m));
  box.scrollTop = box.scrollHeight;
}

function upsertMessage(m) {
  if (m.peer_id !== current) return;
  const i = messages.findIndex((x) => x.id === m.id);
  if (i >= 0) messages[i] = m;
  else messages.push(m);
  renderMessages();
}

async function sendText() {
  const t = $('#text-input').value.trim();
  if (!t || !current) return;
  $('#text-input').value = '';
  const m = await api('/api/send-text', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ peer_id: current, text: t }),
  });
  upsertMessage(m);
}

async function sendFiles(files) {
  if (!current) return;
  for (const f of files) {
    const fd = new FormData();
    fd.append('file', f);
    const m = await api('/api/send-file?peer=' + encodeURIComponent(current), {
      method: 'POST',
      body: fd,
    });
    upsertMessage(m);
  }
}

const es = new EventSource('/api/events');
es.onmessage = (e) => {
  const ev = JSON.parse(e.data);
  if (ev.type === 'peers') refreshPeers();
  if (ev.type === 'message' || ev.type === 'message_update') upsertMessage(ev.message);
  if (ev.type === 'progress') {
    const el = document.querySelector(`.msg[data-id="${ev.message_id}"] .bar i`);
    if (el) el.style.width = ((ev.sent * 100) / ev.total).toFixed(1) + '%';
  }
};

$('#send-btn').onclick = sendText;
$('#text-input').addEventListener('keydown', (e) => {
  if (e.key === 'Enter' && !e.shiftKey) {
    e.preventDefault();
    sendText();
  }
});
$('#file-btn').onclick = () => $('#file-input').click();
$('#file-input').onchange = () => {
  sendFiles($('#file-input').files);
  $('#file-input').value = '';
};
$('#messages').addEventListener('dragover', (e) => e.preventDefault());
$('#messages').addEventListener('drop', (e) => {
  e.preventDefault();
  sendFiles(e.dataTransfer.files);
});

(async () => {
  self = await api('/api/self');
  $('#self-name').textContent = '本机：' + self.name;
  document.title = '多端互通 - ' + self.name;
  await refreshPeers();
  setInterval(refreshPeers, 5000);  // 兜底轮询，处理设备下线
})();
```

- [ ] **Step 4: 手动浏览器验证（双实例）**

```bash
rm -rf /tmp/dd_a /tmp/dd_b /tmp/dd_b_dl
./build/duoduan --data-dir /tmp/dd_a --port 8520 --name 甲 &
./build/duoduan --data-dir /tmp/dd_b --port 8530 --name 乙 --download-dir /tmp/dd_b_dl &
```

浏览器打开两个标签页 `http://localhost:8520` 和 `http://localhost:8530`，核对清单：

- [ ] 左侧能看到对方设备且绿点在线
- [ ] 甲选中乙，发文字，乙的页面**无需刷新**即出现绿色气泡（甲侧）/白色气泡（乙侧）
- [ ] 甲发送一个大文件（如 100MB），甲侧出现进度条且推进，完成后显示"已发送"状态
- [ ] 乙侧收到文件消息，显示文件名、大小和保存路径，文件确实在 /tmp/dd_b_dl/
- [ ] 拖拽文件到聊天区可发送
- [ ] kill 掉乙后甲发消息显示"发送失败 · 重试"；重启乙后点重试成功
- [ ] 刷新页面后历史记录仍在

验证完 `kill %1 %2`。

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: 聊天式前端界面（设备列表/气泡/进度条/重试/拖拽）"
```

---

### Task 14: README 与收尾

**Files:**
- Create: `README.md`

- [ ] **Step 1: 全量测试 + 完整双实例回归**

```bash
cmake -B build && cmake --build build -j && ./build/unit_tests
```

预期：全部 PASS。再按任务 13 Step 4 清单做一遍手动回归。

- [ ] **Step 2: 写 README.md**

````markdown
# 多端互通

类似微信文件传输助手的局域网互传工具：聊天式界面，支持文字消息与文件传输，
自动发现同一局域网内的设备，历史记录本地保存（SQLite）。

## 构建（Linux）

```bash
sudo apt install g++ cmake          # 如未安装
cmake -B build && cmake --build build -j
```

## 构建（Windows）

方式一（Visual Studio）：安装 VS2019+ 的"使用 C++ 的桌面开发"，然后：

```bat
cmake -B build
cmake --build build --config Release
```

方式二（MSYS2/MinGW）：

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake
cmake -B build -G "MinGW Makefiles" && cmake --build build -j
```

注意：首次运行时允许 Windows 防火墙的"专用网络"访问，否则无法被发现。

## 运行

```bash
./build/duoduan          # 在仓库根目录运行（需要找到 web/ 目录）
```

浏览器打开 http://localhost:8520 （程序启动时会打印实际端口）。
两台电脑都启动后会自动互相发现。

命令行参数：

| 参数 | 说明 | 默认 |
|---|---|---|
| `--port N` | HTTP 端口 | 8520（占用时自动+1） |
| `--name 名称` | 设备显示名 | 主机名 |
| `--download-dir 路径` | 接收文件保存目录 | ~/Downloads/多端互通 |
| `--data-dir 路径` | 配置与历史记录目录 | ~/.duoduan |

## 测试

```bash
./build/unit_tests
```

## 安全说明

仅限可信局域网使用：当前版本无身份验证，局域网内任何设备都可向你发送
文件并访问页面。配对 PIN 码在后续版本提供。

## 路线图

- [ ] 手机浏览器接入（手机 ↔ 电脑会话）
- [ ] 配对 PIN 码
````

- [ ] **Step 3: 验证 Linux↔Windows 注意事项已覆盖**

确认 README 中包含：防火墙放行说明、两种 Windows 构建方式。真机 Windows 验证留待用户实际操作（本机无 Windows 环境）。

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "docs: README（构建/运行/参数说明）"
```

---

## 验收标准（对照设计文档）

- [x→任务8] 设备自动发现（UDP 广播，10 秒离线判定）
- [x→任务9] 文字消息 + SSE 实时刷新
- [x→任务10/11] 文件流式收发、.part 临时文件、同名去重、暂存策略
- [x→任务11] 传输进度条
- [x→任务12] 失败重试
- [x→任务6] SQLite 历史记录
- [x→任务8] 端口占用自动递增
- [x→任务13] 聊天式界面（气泡/设备列表/拖拽）
- [x→任务14] Windows 构建支持（CMake ws2_32 / MSVC utf-8 已配置）
