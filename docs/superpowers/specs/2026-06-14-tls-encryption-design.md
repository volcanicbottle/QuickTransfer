# 设计：传输加密（自签 HTTPS + PC↔PC 指纹固定）

日期：2026-06-14
状态：已确认

## 背景

当前全程明文 HTTP——消息、文件、配对 PIN、`X-Pair-Token`、手机 `qt_token` cookie
都在网络上裸奔。用户打算在**公共网络**使用，同一 WiFi 上任何人抓包即可窃取内容与凭证。

### 威胁模型与取舍（已确认）

- **被动嗅探**（公共 WiFi 上抓包）：高频且容易——首要防护目标。自签 HTTPS 即可根除。
- **主动 MITM**（流氓 AP / ARP 欺骗冒充设备）：
  - PC↔PC 路径：用配对时固定证书指纹（TOFU）廉价防住，纳入本设计。
  - 手机/他机浏览器路径：彻底防 MITM 需让用户安装受信 CA，体验过重，**不做**；
    该路径为「加密 + 首次信任」。

可接受的成本：
1. 引入 OpenSSL 构建依赖（项目首个外部依赖；三平台 CI 需装 OpenSSL dev）。
2. 自签证书导致浏览器首次「不安全」警告，需手动点继续。

不做（YAGNI）：
- 不做受信 CA / 证书安装流程。
- 不保留 HTTP 回退（回退会架空加密）。
- 不做应用层 AES（凭证在头部/cookie，HTTPS 才是正解）。

## 1. 证书与指纹模块 `src/tls.{h,cpp}`

隔离所有 OpenSSL/加密代码，单一职责。

接口：
```cpp
namespace tls {
struct CertPaths { std::filesystem::path cert, key; };
// 确保 data_dir 下存在自签证书；缺失则用 OpenSSL X509 API 进程内生成。
// SAN 含 DNS:localhost、IP:127.0.0.1 及传入的 lan_ips。返回证书/私钥路径。
CertPaths ensure_cert(const std::filesystem::path& data_dir,
                      const std::string& common_name,
                      const std::vector<std::string>& san_ips);
// 对 PEM 证书算 SHA-256 指纹（DER 编码上计算），返回小写十六进制（无冒号）。
std::string fingerprint_sha256(const std::string& cert_pem);
}
```

实现要点：
- 进程内生成：`EVP_PKEY`（**RSA 2048**，兼容性最广，避免个别浏览器对 EC 自签的边角问题）
  + `X509`，自签，有效期 10 年，
  CN = 设备名，SAN 含 localhost / 127.0.0.1 / 各 lan_ips。写 `cert.pem`、`key.pem`；
  私钥文件权限 0600（POSIX `chmod`；Windows 跳过）。
- **不** shell 调用 `openssl` 二进制——跨平台且无运行时依赖。
- `fingerprint_sha256`：PEM→X509→`i2d_X509`(DER)→`SHA256`→hex。须与
  `openssl x509 -noout -fingerprint -sha256` 输出（去掉冒号、转小写）一致。
- 证书存于 `data_dir`（与 config.json、history.db 同处）。

## 2. 服务器切换到 TLS

- `App` 的 `httplib::Server svr_` 改为 `httplib::SSLServer svr_`（仍是值成员）。`SSLServer`
  构造时即需 cert/key，故在 **App 构造函数初始化列表**中先生成证书再初始化 svr_：新增一个
  `tls::CertPaths certs_` 成员（声明于 svr_ 之前），用 `certs_(tls::ensure_cert(cfg_.data_dir,
  cfg_.name, util::lan_ips()))` 初始化，再 `svr_(certs_.cert.string().c_str(),
  certs_.key.string().c_str())`。成员初始化按声明顺序，cfg_ 在前可被引用。
- `SSLServer` 继承 `httplib::Server` 接口，`bind_to_port` / `listen_after_bind` / `stop` /
  `set_pre_routing_handler` / 路由注册等调用全部不变。端口仍从 8520 起，**协议为 HTTPS**。
- 启动打印改为 `https://localhost:%d`。
- CMake：`find_package(OpenSSL REQUIRED)`；对 `duoduan` 与 `unit_tests` 两个目标
  `target_compile_definitions(... CPPHTTPLIB_OPENSSL_SUPPORT)` 并
  `target_link_libraries(... OpenSSL::SSL OpenSSL::Crypto)`。三平台 CI workflow 增加
  安装 OpenSSL dev 的步骤（Linux: libssl-dev；macOS: openssl@3；Windows: 经 vcpkg 或预装）。
- UDP 发现（discovery）不受影响，仍明文广播 id/name/port（不含敏感信息）。

## 3. PC↔PC 证书指纹固定（防 MITM，TOFU）

- `pairings` 表加 `fingerprint TEXT NOT NULL DEFAULT ''` 列；旧库 ALTER 迁移（与
  phones.device 同模式，忽略 duplicate column 错误）。`AuthStore::save_pairing`
  增加 fingerprint 参数；新增 `get_pairing_fingerprint(peer_id, out)` 或在现有
  查询中带出。
- **配对（发起方 `/api/pair`）**：本机用 `httplib::SSLClient` 连对方 HTTPS，
  PIN 校验通过、拿到 secret 的同时，取对方服务器证书算指纹，连同 secret 一起
  `save_pairing(peer_id, secret, name, fingerprint)`。**首次配对即信任（TOFU）**。
- **后续发送（`send_text` / `start_file_send`）**：用 `httplib::SSLClient`，关闭默认
  CA 校验（`set_server_certificate_verification(false)`），通过 `ssl_context()` 装一个
  自定义 OpenSSL verify 回调，比对握手证书指纹与 `pairings.fingerprint`；不符则连接
  视为失败，按现有 403 路径处理（`remove_pairing` + 通知前端重新配对）。
- **被动接收方** (`/peer/pair`、`/peer/text`、`/peer/file`) 逻辑不变（仍验
  `X-Pair-Token`），只是现在跑在 TLS 之上。

### 实现风险（写计划时坐实）

自定义指纹校验依赖 httplib `SSLClient::ssl_context()` + OpenSSL verify 回调。
该 API 在当前 vendored httplib 存在（已确认有 `SSLServer`/SSL 支持）。退路：握手后用
`SSL_get_peer_certificate` 取证书自行比对。计划阶段先用最小示例验证可行性再铺开。

## 4. 手机/他机浏览器

- QR 内容与显示网址改为 `https://<ip>:8520/?pin=<PIN>`。
- 浏览器首次访问自签 HTTPS 必有「不安全/您的连接不是私密连接」警告。QR 浮层
  （`web/index.html` 的 `#qr-hint` 与文字区）补充引导："首次会提示不安全，这是正常的——
  因为用的是本机自签证书；点『高级 → 继续前往』即可。"
- 该路径定位为加密 + 首次信任，不做强 MITM 防护。

## 5. 可选（已问用户，默认纳入轻量项）

- `/api/self`（localhost 分支）额外返回本机证书指纹 `fingerprint`，前端侧边栏
  小字显示，便于用户肉眼向对方核对（增强 TOFU 首次配对的可信度）。实现成本低，纳入。

## 测试

单元测试（doctest，`tls` 模块可独立测）：
- `tls::ensure_cert`：首次生成 cert.pem/key.pem；能被 OpenSSL 重新解析为合法 X509；
  含预期 SAN（localhost、127.0.0.1、给定 IP）；二次调用复用不重生成。
- `tls::fingerprint_sha256`：对固定测试证书的指纹与 `openssl x509 -fingerprint -sha256`
  去冒号小写后一致；同证书多次计算稳定。
- `AuthStore`：`pairings.fingerprint` 列读写；旧库（无 fingerprint 列）ALTER 迁移后查询正常，
  旧记录指纹为空串。

手工验证：
- 浏览器经 `https://localhost:8520` 打开（首次警告后）功能正常。
- 手机扫码经 HTTPS 接入、收发正常。
- 两台电脑配对 → 互发；篡改/替换一端证书后对方连接被指纹校验拒绝。
- 三平台 CI 构建通过（含 OpenSSL 链接）。

## 影响的文件

| 文件 | 动作 |
|---|---|
| `src/tls.h` / `src/tls.cpp` | 新建：证书生成 + 指纹 |
| `src/server.h` / `src/server.cpp` | SSLServer、run() 生成证书、启动文案、send_text/start_file_send 用 SSLClient+pinning、/api/self 带 fingerprint |
| `src/server_auth.cpp` | /api/pair 取并存对方指纹 |
| `src/auth.h` / `src/auth.cpp` | pairings.fingerprint 列 + 迁移 + save/get |
| `CMakeLists.txt` | find OpenSSL、CPPHTTPLIB_OPENSSL_SUPPORT、链接 |
| `.github/workflows/ci.yml` | 三平台装 OpenSSL dev |
| `web/index.html` / `web/app.js` | QR 用 https、警告引导文案、可选显示本机指纹 |
| `tests/test_tls.cpp` | 新建：证书/指纹单测 |
| `tests/test_auth.cpp` | pairings.fingerprint 迁移/读写 |
| `README.md` | 说明 HTTPS、首次警告、配对指纹 |
