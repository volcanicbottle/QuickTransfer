# 传输加密（自签 HTTPS + PC↔PC 指纹固定）实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把局域网传输工具从明文 HTTP 切换到自签 HTTPS（防被动嗅探），并在电脑↔电脑路径用配对时固定的证书指纹防 MITM。

**Architecture:** 新增隔离的 `tls` 模块（OpenSSL 进程内生成自签证书 + 计算/校验 SHA-256 指纹 + 提供 OpenSSL 证书校验回调）。`App` 的 `httplib::Server` 换成 `httplib::SSLServer`（构造期生成证书）。电脑互发改用 `httplib::SSLClient`，关闭默认 CA 校验改为通过 `SSL_CTX_set_cert_verify_callback` 做指纹固定；配对时首次信任并记录对方指纹（TOFU）。手机/他机浏览器走 HTTPS（首次浏览器警告，引导用户继续）。

**Tech Stack:** C++17 / cpp-httplib（开 `CPPHTTPLIB_OPENSSL_SUPPORT`）/ OpenSSL 3 / SQLite3 / nlohmann::json / doctest / 原生 JS

---

## ⚠️ 前置条件（执行前必须满足）

本计划全程需要 OpenSSL 开发头文件才能编译。开始前请在 shell 运行：
```bash
sudo apt-get update && sudo apt-get install -y libssl-dev   # Linux（本机）
```
（macOS: `brew install openssl@3`；Windows CI: vcpkg/预装。）运行时库 `libssl.so.3`/
`libcrypto.so.3` 已在本机，但缺 `-dev` 头（`/usr/include/openssl/ssl.h` 不存在）。
若执行者无 sudo 权限，**报告 BLOCKED**，请用户先安装。

---

## 文件结构

| 文件 | 职责 | 动作 |
|---|---|---|
| `src/tls.h` / `src/tls.cpp` | 自签证书生成、SHA-256 指纹、OpenSSL 校验回调（capture/pin） | 新建 |
| `CMakeLists.txt` | find OpenSSL、`CPPHTTPLIB_OPENSSL_SUPPORT`、链接 | 修改 |
| `.github/workflows/ci.yml` | 三平台安装 OpenSSL dev | 修改 |
| `src/auth.h` / `src/auth.cpp` | `pairings.fingerprint` 列 + 迁移 + save/get | 修改 |
| `src/server.h` / `src/server.cpp` | `SSLServer` 成员、构造期证书、启动文案、SSLClient+pinning、/api/self 带指纹 | 修改 |
| `src/server_auth.cpp` | /api/pair 用 SSLClient 并捕获对方指纹存库 | 修改 |
| `web/index.html` / `web/app.js` | QR 用 https、首次警告引导、（可选）显示本机指纹 | 修改 |
| `tests/test_tls.cpp` | 证书/指纹单测 | 新建 |
| `tests/test_auth.cpp` | pairings.fingerprint 迁移/读写 | 修改 |
| `README.md` | HTTPS、首次警告、配对指纹说明 | 修改 |

> CMake 用 `file(GLOB ...)`，新 `.cpp` 自动纳入 `duoduan` 与 `unit_tests`。

---

## Task 1: 构建系统接入 OpenSSL

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: 确认前置头文件存在**

Run: `ls /usr/include/openssl/ssl.h && echo OK`
Expected: 打印路径 + `OK`。若失败 → 先装 libssl-dev（见前置条件），装不了则 BLOCKED。

- [ ] **Step 2: CMakeLists.txt 加 OpenSSL**

在 `find_package(Threads REQUIRED)` 之后加：
```cmake
find_package(OpenSSL REQUIRED)
```
在 `add_executable(duoduan ...)` 之后、其 `target_link_libraries` 处，改为同时定义宏并链接：
```cmake
target_compile_definitions(duoduan PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(duoduan PRIVATE sqlite3_lib Threads::Threads ${CMAKE_DL_LIBS}
                      OpenSSL::SSL OpenSSL::Crypto)
```
同样地，`add_executable(unit_tests ...)` 之后：
```cmake
target_compile_definitions(unit_tests PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(unit_tests PRIVATE sqlite3_lib Threads::Threads ${CMAKE_DL_LIBS}
                      OpenSSL::SSL OpenSSL::Crypto)
```
（保留原有 `if(WIN32) ... ws2_32 iphlpapi` 块不变。）

- [ ] **Step 3: 重新配置并构建（验证 SSL 宏开启后 httplib 仍编译）**

Run: `rm -rf build && cmake -S . -B build && cmake --build build -j 2>&1 | tail -3`
Expected: 链接成功（此时还没用到 SSL 代码，但 httplib 的 SSL 分支会随宏编译进来，验证 OpenSSL 头/库可用）。

- [ ] **Step 4: 跑现有测试确保未破坏**

Run: `./build/unit_tests 2>&1 | tail -2`
Expected: 28/28 通过。

- [ ] **Step 5: ci.yml 三平台装 OpenSSL dev**

在 `.github/workflows/ci.yml` 的 `- uses: actions/checkout@v4` 之后、`配置` 步骤之前，加一步：
```yaml
      - name: 安装 OpenSSL (Linux)
        if: runner.os == 'Linux'
        run: sudo apt-get update && sudo apt-get install -y libssl-dev

      - name: 安装 OpenSSL (macOS)
        if: runner.os == 'macOS'
        run: brew install openssl@3

      - name: 安装 OpenSSL (Windows)
        if: runner.os == 'Windows'
        run: vcpkg install openssl:x64-windows
```
并把 Windows 之外的 `配置` 步骤改为能找到 OpenSSL；Windows 用 vcpkg 工具链。把原
`- name: 配置 / run: cmake -B build` 替换为：
```yaml
      - name: 配置 (Linux/macOS)
        if: runner.os != 'Windows'
        run: cmake -B build

      - name: 配置 (Windows)
        if: runner.os == 'Windows'
        run: cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

- [ ] **Step 6: 提交**

```bash
git add CMakeLists.txt .github/workflows/ci.yml
git commit -m "build: 接入 OpenSSL，开启 httplib SSL 支持 + 三平台 CI 安装"
```

---

## Task 2: `tls` 模块——证书生成、指纹、校验回调

**Files:**
- Create: `src/tls.h`
- Create: `src/tls.cpp`
- Test: `tests/test_tls.cpp`

- [ ] **Step 1: 写失败测试 `tests/test_tls.cpp`**

```cpp
#include "doctest.h"
#include "tls.h"
#include "util.h"
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <filesystem>
#include <fstream>
#include <cstdio>

static std::string read_file(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), {});
}

TEST_CASE("tls::ensure_cert 生成可解析的自签证书并复用") {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / ("dd_tls_" + util::gen_id());
  fs::create_directories(dir);
  auto paths = tls::ensure_cert(dir, "测试机", {"192.168.1.50"});
  REQUIRE(fs::exists(paths.cert));
  REQUIRE(fs::exists(paths.key));

  // 证书可被 OpenSSL 解析
  std::string pem = read_file(paths.cert);
  BIO* bio = BIO_new_mem_buf(pem.data(), (int)pem.size());
  X509* x = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  REQUIRE(x != nullptr);

  // SAN 含 localhost / 127.0.0.1 / 给定 IP
  char buf[4096] = {0};
  GENERAL_NAMES* gens = (GENERAL_NAMES*)X509_get_ext_d2i(x, NID_subject_alt_name, nullptr, nullptr);
  REQUIRE(gens != nullptr);
  std::string san_dump;
  for (int i = 0; i < sk_GENERAL_NAME_num(gens); ++i) {
    GENERAL_NAME* g = sk_GENERAL_NAME_value(gens, i);
    if (g->type == GEN_DNS) {
      ASN1_STRING* s = g->d.dNSName;
      san_dump += std::string((const char*)ASN1_STRING_get0_data(s), ASN1_STRING_length(s)) + ";";
    } else if (g->type == GEN_IPADD) {
      ASN1_OCTET_STRING* ip = g->d.iPAddress;
      const unsigned char* d = ASN1_STRING_get0_data(ip);
      if (ASN1_STRING_length(ip) == 4) {
        std::snprintf(buf, sizeof(buf), "%d.%d.%d.%d", d[0], d[1], d[2], d[3]);
        san_dump += std::string(buf) + ";";
      }
    }
  }
  GENERAL_NAMES_free(gens);
  CHECK(san_dump.find("localhost") != std::string::npos);
  CHECK(san_dump.find("127.0.0.1") != std::string::npos);
  CHECK(san_dump.find("192.168.1.50") != std::string::npos);
  X509_free(x);
  BIO_free(bio);

  // 复用：再次调用不改变文件内容
  std::string cert_before = pem;
  auto paths2 = tls::ensure_cert(dir, "测试机", {"192.168.1.50"});
  CHECK(read_file(paths2.cert) == cert_before);
  fs::remove_all(dir);
}

TEST_CASE("tls::fingerprint_sha256 与 openssl CLI 一致且稳定") {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / ("dd_fp_" + util::gen_id());
  fs::create_directories(dir);
  auto paths = tls::ensure_cert(dir, "fp", {});
  std::string pem = read_file(paths.cert);
  std::string fp1 = tls::fingerprint_sha256(pem);
  std::string fp2 = tls::fingerprint_sha256(pem);
  CHECK(fp1 == fp2);                       // 稳定
  CHECK(fp1.size() == 64);                 // 32 字节 = 64 hex
  CHECK(fp1.find_first_not_of("0123456789abcdef") == std::string::npos);  // 小写 hex

  // 与 openssl CLI 比对（去冒号转小写）
  std::string cmd = "openssl x509 -in '" + paths.cert.string() +
                    "' -noout -fingerprint -sha256";
  FILE* pipe = popen(cmd.c_str(), "r");
  REQUIRE(pipe != nullptr);
  char out[512] = {0};
  fgets(out, sizeof(out), pipe);
  pclose(pipe);
  std::string cli(out);  // 形如 "sha256 Fingerprint=AB:CD:..."
  std::string hex;
  for (char c : cli) {
    if (std::isxdigit((unsigned char)c)) hex += (char)std::tolower((unsigned char)c);
  }
  CHECK(hex == fp1);
  fs::remove_all(dir);
}
```

- [ ] **Step 2: 跑测试确认编译失败**（`tls.h` 不存在）。

Run: `cmake --build build -j 2>&1 | tail -5`  Expected: FAIL。

- [ ] **Step 3: 创建 `src/tls.h`**

```cpp
#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <openssl/ossl_typ.h>  // X509_STORE_CTX 前置

namespace tls {

struct CertPaths {
  std::filesystem::path cert;  // PEM
  std::filesystem::path key;   // PEM
};

// 确保 data_dir 下存在自签证书；缺失则进程内生成（RSA 2048，10 年，
// SAN 含 localhost/127.0.0.1 与 san_ips）。已存在则原样复用。
CertPaths ensure_cert(const std::filesystem::path& data_dir,
                      const std::string& common_name,
                      const std::vector<std::string>& san_ips);

// 对 PEM 证书算 SHA-256 指纹（DER 上计算），返回 64 位小写十六进制（无冒号）。
std::string fingerprint_sha256(const std::string& cert_pem);

// OpenSSL 证书校验回调（配 SSL_CTX_set_cert_verify_callback 使用）：
// capture：永远信任(返回1)并把叶证书指纹写入 *(std::string*)arg —— 用于配对首次信任(TOFU)。
int capture_verify_cb(X509_STORE_CTX* ctx, void* arg);
// pin：仅当叶证书指纹 == *(const std::string*)arg 时返回1，否则0 —— 用于后续连接固定。
int pin_verify_cb(X509_STORE_CTX* ctx, void* arg);

}  // namespace tls
```

- [ ] **Step 4: 创建 `src/tls.cpp`**

```cpp
#include "tls.h"
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/bn.h>
#include <fstream>
#include <cstdio>

namespace tls {

static std::string to_hex_lower(const unsigned char* p, size_t n) {
  static const char* h = "0123456789abcdef";
  std::string s;
  s.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) { s += h[p[i] >> 4]; s += h[p[i] & 15]; }
  return s;
}

std::string fingerprint_sha256(const std::string& cert_pem) {
  BIO* bio = BIO_new_mem_buf(cert_pem.data(), (int)cert_pem.size());
  if (!bio) return "";
  X509* x = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (!x) return "";
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned int len = 0;
  // X509_digest 对 DER 编码算摘要，与 openssl x509 -fingerprint -sha256 一致
  int ok = X509_digest(x, EVP_sha256(), md, &len);
  X509_free(x);
  if (!ok) return "";
  return to_hex_lower(md, len);
}

static std::string fingerprint_of_x509(X509* x) {
  if (!x) return "";
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned int len = 0;
  if (!X509_digest(x, EVP_sha256(), md, &len)) return "";
  return to_hex_lower(md, len);
}

int capture_verify_cb(X509_STORE_CTX* ctx, void* arg) {
  auto* out = static_cast<std::string*>(arg);
  X509* leaf = X509_STORE_CTX_get0_cert(ctx);
  if (out && leaf) *out = fingerprint_of_x509(leaf);
  return 1;  // TOFU：首次一律信任
}

int pin_verify_cb(X509_STORE_CTX* ctx, void* arg) {
  auto* expected = static_cast<const std::string*>(arg);
  X509* leaf = X509_STORE_CTX_get0_cert(ctx);
  if (!expected || !leaf) return 0;
  return fingerprint_of_x509(leaf) == *expected ? 1 : 0;
}

CertPaths ensure_cert(const std::filesystem::path& data_dir,
                      const std::string& common_name,
                      const std::vector<std::string>& san_ips) {
  namespace fs = std::filesystem;
  CertPaths p{data_dir / "cert.pem", data_dir / "key.pem"};
  std::error_code ec;
  if (fs::exists(p.cert, ec) && fs::exists(p.key, ec)) return p;  // 复用
  fs::create_directories(data_dir, ec);

  EVP_PKEY* pkey = EVP_PKEY_new();
  RSA* rsa = RSA_new();
  BIGNUM* bn = BN_new();
  BN_set_word(bn, RSA_F4);
  RSA_generate_key_ex(rsa, 2048, bn, nullptr);
  EVP_PKEY_assign_RSA(pkey, rsa);  // pkey 接管 rsa
  BN_free(bn);

  X509* x = X509_new();
  X509_set_version(x, 2);  // v3
  ASN1_INTEGER_set(X509_get_serialNumber(x), 1);
  X509_gmtime_adj(X509_get_notBefore(x), 0);
  X509_gmtime_adj(X509_get_notAfter(x), 60LL * 60 * 24 * 365 * 10);  // 10 年
  X509_set_pubkey(x, pkey);

  X509_NAME* name = X509_get_subject_name(x);
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8,
                             (const unsigned char*)common_name.c_str(), -1, -1, 0);
  X509_set_issuer_name(x, name);  // 自签：issuer == subject

  // SAN：localhost、127.0.0.1、各 san_ips
  std::string san = "DNS:localhost,IP:127.0.0.1";
  for (const auto& ip : san_ips) san += ",IP:" + ip;
  X509V3_CTX v3ctx;
  X509V3_set_ctx_nodb(&v3ctx);
  X509V3_set_ctx(&v3ctx, x, x, nullptr, nullptr, 0);
  X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &v3ctx, NID_subject_alt_name, san.c_str());
  if (ext) { X509_add_ext(x, ext, -1); X509_EXTENSION_free(ext); }

  X509_sign(x, pkey, EVP_sha256());

  // 写文件
  FILE* fc = std::fopen(p.cert.string().c_str(), "wb");
  if (fc) { PEM_write_X509(fc, x); std::fclose(fc); }
  FILE* fk = std::fopen(p.key.string().c_str(), "wb");
  if (fk) { PEM_write_PrivateKey(fk, pkey, nullptr, nullptr, 0, nullptr, nullptr); std::fclose(fk); }
#ifndef _WIN32
  fs::permissions(p.key, fs::perms::owner_read | fs::perms::owner_write,
                  fs::perm_options::replace, ec);  // 0600
#endif

  X509_free(x);
  EVP_PKEY_free(pkey);
  return p;
}

}  // namespace tls
```

- [ ] **Step 5: 构建并跑 tls 测试**

Run: `cmake --build build -j && ./build/unit_tests -tc="tls::ensure_cert 生成可解析的自签证书并复用","tls::fingerprint_sha256 与 openssl CLI 一致且稳定"`
Expected: PASS。再跑全量 `./build/unit_tests` 全绿。

- [ ] **Step 6: 提交**

```bash
git add src/tls.h src/tls.cpp tests/test_tls.cpp
git commit -m "feat: tls 模块——自签证书生成、SHA-256 指纹、校验回调"
```

---

## Task 3: `pairings.fingerprint` 列

**Files:**
- Modify: `src/auth.h`
- Modify: `src/auth.cpp`
- Test: `tests/test_auth.cpp`

- [ ] **Step 1: 在 test_auth.cpp 末尾加失败测试**

```cpp
TEST_CASE("AuthStore pairings.fingerprint 读写与迁移") {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / ("dd_fp_" + util::gen_id());
  fs::create_directories(dir);
  auto db = dir / "t.db";
  {
    AuthStore a(db);
    a.save_pairing("p1", "secret1", "甲", "aabbccdd");
    std::string s, fp;
    REQUIRE(a.get_pairing_secret("p1", s));
    CHECK(s == "secret1");
    REQUIRE(a.get_pairing_fingerprint("p1", fp));
    CHECK(fp == "aabbccdd");
    a.set_pairing_fingerprint("p1", "eeff0011");  // TOFU 更新
    REQUIRE(a.get_pairing_fingerprint("p1", fp));
    CHECK(fp == "eeff0011");
    std::string s2;
    REQUIRE(a.get_pairing_secret("p1", s2));       // secret 不受影响
    CHECK(s2 == "secret1");
  }
  // 旧库（无 fingerprint 列）迁移
  auto db2 = dir / "old.db";
  {
    sqlite3* raw = nullptr;
    REQUIRE(sqlite3_open(db2.u8string().c_str(), &raw) == SQLITE_OK);
    sqlite3_exec(raw,
        "CREATE TABLE pairings(peer_id TEXT PRIMARY KEY, secret TEXT NOT NULL,"
        " name TEXT NOT NULL DEFAULT '');"
        "INSERT INTO pairings(peer_id,secret,name) VALUES('old','s','旧');",
        nullptr, nullptr, nullptr);
    sqlite3_close(raw);
  }
  {
    AuthStore a(db2);  // 构造应 ALTER 补列
    std::string fp;
    REQUIRE(a.get_pairing_fingerprint("old", fp));
    CHECK(fp == "");  // 旧记录指纹空串
  }
  fs::remove_all(dir);
}
```

- [ ] **Step 2: 跑测试确认失败**（`save_pairing` 签名不符 / `get_pairing_fingerprint` 未声明）。

- [ ] **Step 3: `src/auth.h`**

把 `save_pairing` 声明改为带 fingerprint 参数，并加 getter：
```cpp
  void save_pairing(const std::string& peer_id, const std::string& secret,
                    const std::string& name, const std::string& fingerprint);
  bool get_pairing_secret(const std::string& peer_id, std::string& out) const;
  bool get_pairing_fingerprint(const std::string& peer_id, std::string& out) const;
  void set_pairing_fingerprint(const std::string& peer_id, const std::string& fp);  // TOFU 首次记录
```

- [ ] **Step 4: `src/auth.cpp`**

(a) 构造函数 `CREATE TABLE` 之后加迁移（紧接现有 phones 的 device 迁移块后）：
```cpp
  {
    char* err = nullptr;
    sqlite3_exec(db_, "ALTER TABLE pairings ADD COLUMN fingerprint TEXT NOT NULL DEFAULT ''",
                 nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
  }
```
(b) 重写 `save_pairing`：
```cpp
void AuthStore::save_pairing(const std::string& peer_id, const std::string& secret,
                             const std::string& name, const std::string& fingerprint) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(
      db_,
      "INSERT OR REPLACE INTO pairings(peer_id,secret,name,fingerprint) VALUES(?,?,?,?)",
      -1, &st, nullptr);
  sqlite3_bind_text(st, 1, peer_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, secret.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, fingerprint.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(st);
  sqlite3_finalize(st);
}
```
(c) 加 `get_pairing_fingerprint`（仿 `get_pairing_secret`）：
```cpp
bool AuthStore::get_pairing_fingerprint(const std::string& peer_id, std::string& out) const {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_, "SELECT fingerprint FROM pairings WHERE peer_id=?", -1, &st, nullptr);
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
```
(d) 加 `set_pairing_fingerprint`（只更新指纹列，用于 TOFU 首次记录）：
```cpp
void AuthStore::set_pairing_fingerprint(const std::string& peer_id, const std::string& fp) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_, "UPDATE pairings SET fingerprint=? WHERE peer_id=?", -1, &st, nullptr);
  sqlite3_bind_text(st, 1, fp.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, peer_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(st);
  sqlite3_finalize(st);
}
```

- [ ] **Step 5: 暂时修复其它 `save_pairing` 调用点以保编译**

`src/server.cpp` 与 `src/server_auth.cpp` 现有对 `save_pairing(...)` 的调用现在缺第 4 参数，会编译失败。本步骤先传空串占位（Task 5 会填真实指纹）：
- `src/server_auth.cpp` 中 `/api/pair` 的 `auth_.save_pairing(peer_id, secret, peer.name);`
  → `auth_.save_pairing(peer_id, secret, peer.name, "");`
- `src/server_auth.cpp` 中 `/peer/pair` 的 `auth_.save_pairing(from_id, secret, from_name);`
  → `auth_.save_pairing(from_id, secret, from_name, "");`
（grep 确认无遗漏：`grep -rn "save_pairing(" src/`，每处都补足 4 个实参。）

- [ ] **Step 6: 构建并测试**

Run: `cmake --build build -j && ./build/unit_tests -tc="AuthStore pairings.fingerprint 读写与迁移"`
然后全量 `./build/unit_tests` 全绿。

- [ ] **Step 7: 提交**

```bash
git add src/auth.h src/auth.cpp src/server.cpp src/server_auth.cpp tests/test_auth.cpp
git commit -m "feat: pairings 表加 fingerprint 列 + 迁移（调用点暂填空串）"
```

---

## Task 4: 服务器切换到 HTTPS（SSLServer）

**Files:**
- Modify: `src/server.h`
- Modify: `src/server.cpp`

- [ ] **Step 1: `src/server.h` —— svr_ 改 SSLServer + 加 certs_ 成员**

顶部加 include：
```cpp
#include "tls.h"
```
把成员 `httplib::Server svr_;` 改为，并在其前加 `certs_`（声明顺序：certs_ 必须在 svr_ 之前）：
```cpp
  tls::CertPaths certs_;
  httplib::SSLServer svr_;
  int port_ = 0;
```
（`quitting_`/`quit_thread_` 保持在 svr_ 之后不变。）

- [ ] **Step 2: `src/server.cpp` —— 构造函数初始化列表生成证书**

把构造函数改为（在 svr_ 初始化前先 ensure_cert，SAN 用当前 lan_ips）：
```cpp
App::App(Config cfg, std::filesystem::path web_dir)
    : cfg_(std::move(cfg)),
      web_dir_(std::move(web_dir)),
      history_(cfg_.db_path()),
      auth_(cfg_.db_path()),
      certs_(tls::ensure_cert(cfg_.data_dir, cfg_.name, util::lan_ips())),
      svr_(certs_.cert.string().c_str(), certs_.key.string().c_str()) {}
```
（需要 `#include "util.h"`，server.cpp 已有。）

- [ ] **Step 3: 启动文案改 https**

`App::run()` 里启动打印从 `已启动：http://localhost:%d` 改为 `已启动：https://localhost:%d`。

- [ ] **Step 4: 构建**

Run: `cmake --build build -j 2>&1 | tail -3`
Expected: 编译链接通过。`./build/unit_tests` 全绿（构造 App 的测试若有，会真正生成证书）。

- [ ] **Step 5: 手工冒烟——HTTPS 起得来**

```bash
rm -rf /tmp/dd_tls; ./build/duoduan --data-dir /tmp/dd_tls --port 8610 >/tmp/dd_tls.log 2>&1 &
sleep 1.5
ls /tmp/dd_tls/cert.pem /tmp/dd_tls/key.pem            # 证书已生成
curl -sk https://127.0.0.1:8610/api/self | head -c 200 # -k 接受自签；应返回 JSON 含 is_remote
curl -s  http://127.0.0.1:8610/api/self                # 纯 http 应失败（已是 https-only）
curl -sk -X POST https://127.0.0.1:8610/api/quit       # {"ok":true} 并停止
cat /tmp/dd_tls.log                                     # 含 https://localhost / 已停止
rm -rf /tmp/dd_tls /tmp/dd_tls.log
```
Expected: cert/key 存在；https 的 /api/self 返回 JSON；http 请求失败；quit 正常退出。
若进程没退或证书没生成，调查后再继续，勿标 DONE。

- [ ] **Step 6: 提交**

```bash
git add src/server.h src/server.cpp
git commit -m "feat: 服务器切换为 SSLServer（自签 HTTPS），构造期生成证书"
```

---

## Task 5: PC↔PC 指纹固定 + /api/self 带本机指纹

**Files:**
- Modify: `src/server.cpp`（send_text、start_file_send、/api/self）
- Modify: `src/server_auth.cpp`（/api/pair 捕获对方指纹）

> 本任务先做一个**最小握手验证**再铺开（pinning 的 OpenSSL 行为是设计中标注的风险点）。

- [ ] **Step 1: 验证 SSLClient + cert_verify_callback 行为（spike）**

临时在一个可丢弃的小程序或直接在 send_text 改造前，用如下方式确认机制：
`httplib::SSLClient` 连本机 8610，
```cpp
httplib::SSLClient cli("127.0.0.1", 8610);
std::string fp;  // 期望/捕获
SSL_CTX_set_cert_verify_callback(cli.ssl_context(), tls::capture_verify_cb, &fp);
auto r = cli.Get("/api/self");
// 断言 r 成功且 fp 为 64 位 hex
```
确认：capture 回调能拿到对方证书指纹且连接成功；把 capture 换成 `pin_verify_cb` + 错误指纹时连接失败（`r == nullptr` 或握手错误）。
**若 `set_cert_verify_callback` 在该 httplib 版本不奏效**（连接在指纹不符时仍成功），改用退路：`set_server_certificate_verification(false)` + 请求后取 `SSL_get_peer_certificate`（经 `cli.ssl_context()` 不易拿到运行时 SSL*，则在 capture 回调里始终返回 1 仅用于取指纹，pinning 改为：先 capture 当前指纹再与存储值比较，不符则丢弃结果并按失败处理）。把最终可行方案记录在提交信息里。

- [ ] **Step 2: `src/server_auth.cpp` —— /api/pair 用 SSLClient 并捕获对方指纹**

把 `/api/pair` 里发起配对的客户端从 `httplib::Client` 改为 `httplib::SSLClient`，并在 POST 前装 capture 回调，成功后把指纹随 secret 存库：
```cpp
    httplib::SSLClient cli(peer.ip, peer.port);
    cli.set_connection_timeout(3);
    cli.set_read_timeout(5);
    cli.set_write_timeout(3);
    std::string peer_fp;
    SSL_CTX_set_cert_verify_callback(cli.ssl_context(), tls::capture_verify_cb, &peer_fp);
    json body{{"from_id", cfg_.id}, {"from_name", cfg_.name}, {"pin", pin}};
    auto r = cli.Post("/peer/pair", body.dump(), "application/json");
```
（其余 PIN/secret 解析逻辑不变。）把 `auth_.save_pairing(peer_id, secret, peer.name, "");`
改为 `auth_.save_pairing(peer_id, secret, peer.name, peer_fp);`。
顶部加 `#include "tls.h"` 与 `#include <openssl/ssl.h>`。

- [ ] **Step 3: `src/server.cpp` send_text —— SSLClient + pinning**

把 `httplib::Client cli(peer.ip, peer.port);` 改为 `httplib::SSLClient`，并在请求前按已配对指纹固定：
```cpp
  std::string secret;
  if (!auth_.get_pairing_secret(peer.id, secret)) { history_.set_status(m.id, "fail"); return; }
  std::string fp;
  auth_.get_pairing_fingerprint(peer.id, fp);
  httplib::SSLClient cli(peer.ip, peer.port);
  cli.set_connection_timeout(3);
  cli.set_read_timeout(3);
  cli.set_write_timeout(3);
  std::string captured;  // fp 为空时 TOFU 捕获本次对端指纹
  if (!fp.empty())
    SSL_CTX_set_cert_verify_callback(cli.ssl_context(), tls::pin_verify_cb, &fp);
  else
    SSL_CTX_set_cert_verify_callback(cli.ssl_context(), tls::capture_verify_cb, &captured);
```
在请求发出、判定成功之后（`r && r->status == 200` 分支处），补 TOFU 记录：
```cpp
  if (fp.empty() && r && !captured.empty()) auth_.set_pairing_fingerprint(peer.id, captured);
```
（其余 POST `/peer/text` + 403 处理不变；fp/captured 在请求期间存活，cli 同步使用，安全。
这样 B→A 方向在 B 首次发送时也会记录 A 的指纹，之后双向都固定。）

- [ ] **Step 4: `src/server.cpp` start_file_send —— SSLClient + pinning**

同理把文件推送的 `httplib::Client cli(peer.ip, peer.port);` 改为 `httplib::SSLClient`，
在设置超时后、`cli.Post(path, ...)` 前加：
```cpp
      std::string fp, captured;
      auth_.get_pairing_fingerprint(m.peer_id, fp);
      if (!fp.empty())
        SSL_CTX_set_cert_verify_callback(cli.ssl_context(), tls::pin_verify_cb, &fp);
      else
        SSL_CTX_set_cert_verify_callback(cli.ssl_context(), tls::capture_verify_cb, &captured);
```
并在判定 `ok = r && r->status == 200;` 之后补 TOFU 记录：
```cpp
      if (fp.empty() && ok && !captured.empty()) auth_.set_pairing_fingerprint(m.peer_id, captured);
```
（`fp`/`captured` 需在整个 Post（含流式回调）期间存活——声明在 Post 之前的同一作用域即可。）
顶部确保 `#include <openssl/ssl.h>`（server.cpp 已 include tls.h via server.h）。

- [ ] **Step 5: `src/server.cpp` /api/self —— localhost 带本机证书指纹**

读取本机证书算指纹并在 localhost 分支返回（供肉眼核对）：在 `/api/self` 的 is_local 块内加：
```cpp
    if (is_local(req)) {
      j["pin"] = cfg_.pin;
      j["lan_ips"] = util::lan_ips();
      std::ifstream cf(certs_.cert, std::ios::binary);
      std::string pem((std::istreambuf_iterator<char>(cf)), {});
      j["fingerprint"] = tls::fingerprint_sha256(pem);
    }
```
（server.cpp 顶部已 `#include <fstream>`。）

- [ ] **Step 6: 构建 + 两机指纹流程手工验证**

Run: `cmake --build build -j 2>&1 | tail -2 && ./build/unit_tests 2>&1 | tail -2`（全绿）。
两实例模拟两台电脑（同机不同 data-dir/port，UDP 发现同段即可；若发现不到可手测 pair 接口）：
```bash
rm -rf /tmp/A /tmp/B
./build/duoduan --data-dir /tmp/A --port 8620 >/tmp/A.log 2>&1 &
./build/duoduan --data-dir /tmp/B --port 8630 >/tmp/B.log 2>&1 &
sleep 2
# 取 B 的指纹（本机视角）
curl -sk https://127.0.0.1:8630/api/self
# 在 A 上对 B 发起配对需要 B 的 PIN（见 B.log），并经发现拿到 B 的 ip/port；
# 简化验证：直接确认 SSLClient 能连到对端 https 且指纹被捕获/比对（见下）
```
关键断言（任选其一证明 pinning 生效）：A 配对 B 后，`/tmp/A` 的 history.db `pairings` 表中
B 的 `fingerprint` 列非空且等于 B 的 `/api/self` 返回指纹；将 B 重新生成证书（删 `/tmp/B/cert.pem key.pem` 重启）后，A 再向 B 发消息应失败（指纹不符）。
清理 `/tmp/A /tmp/B *.log`，kill 后台进程。
若 pinning 行为与预期不符，回到 Step 1 的退路方案，**勿标 DONE**。

- [ ] **Step 7: 提交**

```bash
git add src/server.cpp src/server_auth.cpp
git commit -m "feat: PC↔PC 证书指纹固定（配对捕获/后续校验）+ /api/self 暴露本机指纹"
```

---

## Task 6: 前端 —— QR 用 https + 首次警告引导 + 显示本机指纹

**Files:**
- Modify: `web/app.js`
- Modify: `web/index.html`

- [ ] **Step 1: `web/app.js` —— QR URL 改 https**

`showQR()` 里 `const url = \`http://${ip}:${self.port}/?pin=${self.pin}\`;` 改为：
```javascript
    const url = `https://${ip}:${self.port}/?pin=${self.pin}`;
```

- [ ] **Step 2: `web/index.html` —— QR 浮层警告引导**

把 `#qr-hint` 文案改为包含首次警告说明：
```html
    <div id="qr-hint">用手机相机/浏览器扫码；需与本机在同一 Wi-Fi。<br>首次会提示「不安全/连接非私密」——这是正常的（本机自签证书），点「高级 → 继续前往」即可。扫码后填设备名即可连接。</div>
```

- [ ] **Step 3: `web/app.js` —— 桌面侧栏显示本机指纹**

在桌面 `else` 分支 `$('#self-pin').textContent = ...` 之后加（self.fingerprint 来自 /api/self）：
```javascript
    if (self.fingerprint)
      $('#self-fp').textContent = '证书指纹：' + self.fingerprint.replace(/(.{2})/g, '$1:').replace(/:$/, '').slice(0, 47) + '…';
```

- [ ] **Step 4: `web/index.html` —— 加指纹显示位**

在 `#self-pin` 的 `</div>`(或同级) 之后、`#self-actions` 之前加：
```html
  <div id="self-fp" title="向对方核对此指纹可确认未被中间人冒充"></div>
```

- [ ] **Step 5: 手工验证**

`cmake --build build -j`（无 C++ 改动也确认树可构建）。
`node --check web/app.js`（语法 OK）。
起服务，浏览器开 `https://localhost:8610/`（点过警告后）功能正常；侧栏显示「证书指纹：…」；
点「📱 手机接入」二维码内容为 `https://...`。

- [ ] **Step 6: 提交**

```bash
git add web/app.js web/index.html
git commit -m "feat: 前端 QR 用 https + 首次警告引导 + 桌面显示本机证书指纹"
```

---

## Task 7: README 更新

**Files:**
- Modify: `README.md`

- [ ] **Step 1: 补充说明**

READ README.md，在合适位置（运行/配对/安全相关）补：
- 现在通过 **HTTPS（自签证书）** 通信，首次用浏览器/手机访问会出现「不安全」警告，
  属正常现象（自签证书），点「高级 → 继续」即可；公共网络下流量已加密，可防被动嗅探。
- **电脑↔电脑**配对时会记录对方证书指纹（首次信任，TOFU），之后若对方证书变化（疑似
  中间人）连接会被拒绝；可在侧栏「证书指纹」处与对方肉眼核对。
- 构建新增依赖 **OpenSSL**（Linux: `libssl-dev`；macOS: `openssl@3`；Windows: vcpkg `openssl`）。

- [ ] **Step 2: 提交**

```bash
git add README.md
git commit -m "docs: README 说明 HTTPS、首次警告与配对指纹"
```

---

## 完成标准

- `./build/unit_tests` 全绿（含 tls 证书/指纹、pairings.fingerprint 迁移）。
- 服务以 HTTPS 跑在 8520；纯 http 请求失败；浏览器/手机经 https 接入功能正常。
- 两台电脑配对后 `pairings.fingerprint` 记录对方指纹；对方证书变更后连接被拒。
- 远程（非 localhost）`/api/self` 仍不含 pin/lan_ips/fingerprint；远程 `/api/quit` 仍 403。
- 三平台 CI（含 OpenSSL 安装）通过。
