# 设计:二维码接入 + 主动停止 + 设备类型区分

日期:2026-06-12
状态:已确认

## 背景

三个改进点:

1. 手机接入需要手输 `http://IP:端口` 和 6 位 PIN,繁琐。希望电脑端显示二维码,手机扫码直达。
2. 目前只能 Ctrl+C 停止服务,没有信号处理,无法确认进程是否真正退出。
3. 另一台电脑用浏览器接入时被当作手机显示(📱),希望区分设备类型。

已确认的产品取舍:

- 扫码后自动填好 PIN,用户仍需输入设备名后点连接(保留一步确认,避免无感接入)。
- 二维码显示在网页侧边栏按钮的弹窗里,纯前端生成,不加 C++ 依赖。
- 停止方式三个都要:网页按钮、Ctrl+C 干净退出、`./duoduan --stop`。
- 远程客户端按 UA 区分手机/电脑,列表分别显示 📱/💻。

## 1. 二维码接入

### 后端

- `GET /api/self` 的 localhost 分支(已返回 `pin` 的那个)新增 `lan_ips` 数组:
  枚举非回环 IPv4 地址,Linux/macOS 用 `getifaddrs`,Windows 用
  `GetAdaptersAddresses`。实现放 `util.cpp`(`util::lan_ips()`)。
- 远程访问不返回 `lan_ips`(与 `pin` 同等待遇)。

### 前端

- 侧边栏本机信息区新增「📱 手机接入」按钮,点击弹出浮层:
  - 二维码内容:`http://<IP>:<端口>/?pin=<PIN>`。
  - 二维码用 vendor 进 `web/` 的单文件 MIT 许可 JS 库(qrcode-generator)离线生成。
  - 浮层同时显示文字网址,方便另一台电脑手输。
  - 多个 IP 时提供下拉切换,默认第一个;一个 IP 都没有时显示提示文案而非空白。
- 手机端打开页面时:若 `is_remote` 且 URL 含 `?pin=`,登录浮层自动填入 PIN,
  光标聚焦设备名输入框;注册成功后 `location.replace('/')` 抹掉带 PIN 的地址。

### 安全说明

PIN 出现在 URL 查询串中,仅存在于二维码与扫码设备的地址栏,注册成功即被
replace 掉;局域网工具可接受。PIN 防爆破机制(PinGuard)不变。

## 2. 主动停止

三条路径汇聚到同一个退出流程:`svr_.stop()` → `listen` 返回 → 停 discovery →
删除 run.json → 打印「已停止」→ 进程退出。

### `POST /api/quit`

- 仅限 localhost(复用 `is_local`,远程请求 403)。
- 响应 `{"ok":true}` 后在独立线程里调 `svr_.stop()`(不能在 handler 里直接停,
  否则响应发不出去)。

### 网页按钮

- 侧边栏新增「⏻ 停止服务」按钮,仅电脑端(非 phoneMode)显示。
- 点击后 `confirm()` 确认,成功后整页替换为「服务已停止,可关闭本页」。

### Ctrl+C / SIGTERM

- `std::signal` 注册 SIGINT、SIGTERM(两平台语义一致,Windows 的 Ctrl+C 也走
  SIGINT),处理函数通过全局 App 指针调 `svr_.stop()`。
- 退出路径打印「已停止」,用户由此确认进程已退。

### `./duoduan --stop`

- 启动时写 `<data_dir>/run.json`(内容:实际监听端口),退出时删除。
- `--stop`:读 run.json 取端口,用 httplib 客户端 POST
  `http://127.0.0.1:<端口>/api/quit`,然后轮询端口直到连接被拒,打印「已停止」。
- 找不到 run.json、或端口连不上:打印「服务未在运行」并清理残留 run.json。
- 选 HTTP 自调用而非 pid 文件+信号:Windows 没有等价的 kill 信号机制,
  HTTP 方案一套代码三平台通用,且与网页按钮共用 `/api/quit`。

### 残留 run.json 的处理

进程被强杀(kill -9、断电)时 run.json 会残留。`--stop` 连不上端口时按
「服务未在运行」处理并删除文件;正常启动时直接覆盖写入,无需额外清理逻辑。

## 3. 设备类型区分

- 前端注册时按 UA 判断:`/Mobi|Android|iPhone/i` 匹配为 `phone`,否则 `pc`,
  随 register 请求体上送 `device` 字段。
- `phones` 表新增 `device` 列(`ALTER TABLE ... ADD COLUMN device TEXT DEFAULT 'phone'`,
  旧库自动迁移,旧记录默认 phone)。`PhoneInfo` 结构体加对应字段。
- 服务端校验:`device` 仅接受 `phone`/`pc`,其他值按 `phone` 处理。
- `/api/peers` 带出该字段;`renderPeers` 按值显示 📱/💻。
- README 补充:另一台电脑无需安装,浏览器打开服务器网址即可作为客户端。

## 测试

在现有 tests/ 框架内新增:

- `/api/quit`:远程访问 403;localhost 调用后 `listen` 返回、进程正常退出。
- `/api/self`:localhost 含 `lan_ips`(数组,可为空);远程不含。
- `/api/phone/register`:带 `device:"pc"` 注册后 `/api/peers` 返回 `device:"pc"`;
  不带 device 或值非法时默认 phone;旧库迁移后查询不报错。
- `--stop`:run.json 缺失时输出「服务未在运行」。

前端扫码、二维码渲染、停止按钮流程手工验证。

## 不做的事(YAGNI)

- 不做终端 ASCII 二维码(需要 C++ 二维码库,网页弹窗已覆盖需求)。
- 不做扫码零输入自动注册(安全取舍,保留设备名确认步骤)。
- 不做 HTTPS/二维码加密(局域网工具,PIN 防爆破已有)。
