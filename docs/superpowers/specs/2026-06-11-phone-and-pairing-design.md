# QuickTransfer 第二阶段 —— 手机接入与统一配对 设计文档

日期：2026-06-11
状态：已确认
前置：第一阶段（docs/superpowers/specs/2026-06-10-lan-transfer-design.md）已完成并合并

## 1. 目标

1. **手机浏览器接入**：手机打开 `http://电脑IP:端口` 即可与该电脑互传文字和文件，无需安装 App。每部手机是独立会话（身份存手机 localStorage + 电脑端 SQLite）。
2. **统一配对鉴权**：手机和电脑用同一套模型——凭对方屏幕上显示的 PIN 换长期凭证，配对一次永久互信。电脑↔电脑的 `/peer/*` 接口不再无鉴权开放。

## 2. 配对模型

每台设备（电脑）持有一个 6 位数字 PIN：

- 首次启动生成并存入 config.json 的 `pin` 字段；终端打印；本机页面（仅 localhost）侧栏显示
- PIN 是"门钥匙"，验证通过后换发长期凭证，PIN 本身不在后续请求中传输

### 2.1 电脑 ↔ 电脑配对

1. A 的设备列表中，未配对设备显示 🔒；点击 → 输入"B 屏幕上显示的 PIN"
2. A 调 B 的 `POST /peer/pair`，body：`{from_id, from_name, pin}`
3. B 验证 PIN：通过则生成一把随机共享密钥 `secret` 返回给 A，双方各自写入 SQLite 表 `pairings(peer_id TEXT PRIMARY KEY, secret TEXT, name TEXT)`——A 存 `(B_id, secret)`，B 存 `(A_id, secret)`。同一把密钥双向使用：发消息时附带，收消息时按 from_id 查表校验。**一次配对，双向互信**
4. 之后 `/peer/text`、`/peer/file` 请求带 HTTP 头 `X-Pair-Token: <secret>`，接收方按 `from_id` 查 pairings 校验，不匹配返回 403
5. 收到 403 时发送方将该配对记录标记失效（对方可能重置了数据），界面回到"未配对"状态可重新配对

### 2.2 手机配对（注册）

1. 手机浏览器首次访问：静态页面正常加载，但 API 请求因无有效 Cookie 返回 401 → 前端切换到内置的**登录视图**（登录视图是单页应用的一部分，不是独立页面）
2. 登录视图输入：设备名（如 "iPhone"）+ 电脑屏幕上的 PIN
3. `POST /api/phone/register`，body：`{phone_id, name, pin}`（phone_id 由前端生成并存 localStorage）→ 验证通过：生成随机 token，写入 SQLite 表 `phones(id TEXT PRIMARY KEY, name TEXT, token TEXT, last_seen INTEGER)`，以 `Set-Cookie: qt_token=...; Max-Age=1年; HttpOnly; SameSite=Lax; Path=/` 下发
4. 之后该手机所有请求凭 Cookie 放行

### 2.3 防爆破

PIN 验证入口（`/peer/pair` 与 `/api/phone/register`）共享一个内存计数器：每次失败延迟 1 秒响应；连续失败 10 次后锁定 5 分钟（期间一律拒绝，不再校验）。成功一次清零。

## 3. 鉴权边界（pre-routing 统一拦截）

| 请求 | 规则 |
|---|---|
| 来源 127.0.0.1（本机浏览器） | 全部放行 |
| 静态文件（html/css/js，不含任何秘密） | 放行 |
| `/api/phone/register` | 放行（内部验 PIN） |
| `/peer/pair` | 放行（内部验 PIN） |
| `/peer/text`、`/peer/file` | 校验 `X-Pair-Token` 头与 pairings 表，失败 403 |
| 其余所有 `/api/*` 来自远程 | 校验 `qt_token` Cookie 与 phones 表，失败 401（前端切登录视图） |
| UDP 发现广播 | 保持开放（只暴露设备名，无法传输） |

注：`/api/pair`（电脑端发起配对的本机接口）仅允许 localhost 调用——手机凭 Cookie 不能替电脑发起配对。

## 4. 手机会话消息流

手机是电脑设备列表中的"虚拟设备"（📱 图标），完全复用 Message 模型与 SSE。

- **手机 → 电脑**：手机页面发文字 `POST /api/phone/send-text {text}`、传文件 `POST /api/phone/send-file`（multipart）→ 文件直接存电脑下载目录（unique_path 去重）→ 入库 `peer_id=phone_id, direction=in, status=ok` → SSE 通知电脑界面。无网络推送环节，本地写入即成功。phone_id 从 Cookie 对应的 phones 记录取得，不信任客户端参数。
- **电脑 → 手机**：电脑界面在手机会话发消息，前端无感（仍调 /api/send-text、/api/send-file）；后端发现 peer_id 是手机（查 phones 表）→ 不做网络推送：文字直接 `ok` 入库；文件存 staging 后直接 `ok` 入库（file_path 指向 staging，**长期保留**供手机随时下载）
- **手机下载**：消息在手机端渲染为下载按钮 → `GET /api/file?id=<消息id>` 流式返回文件。越权防护：手机 token 只能下载/拉取 `peer_id == 自己 phone_id` 的消息；`/api/messages?peer=` 同样限制。localhost 不受限，且电脑界面给收到的文件也加同样的下载按钮（副产物：不用翻文件夹）
- **手机视角方向翻转**：手机端 `direction=in`（电脑收到的=手机发的）渲染在右侧绿色气泡，`out` 在左侧

## 5. 在线状态

- 手机前端每 10 秒 `POST /api/phone/heartbeat` 刷新 `phones.last_seen`；电脑端设备列表中手机 `online = now - last_seen < 30s`
- `/api/peers` 返回合并列表：UDP 发现的电脑（带 `paired` 字段，查 pairings 表）+ phones 表里的手机（`type:"phone"`）

## 6. 前端变化

- **远程检测**：`/api/self` 增加 `is_remote` 字段（按请求来源判定）。远程 → 手机模式
- **手机模式**：隐藏设备列表侧栏，整页为"与这台电脑"的单会话视图（顶部显示电脑名）；气泡方向翻转；响应式 CSS 适配竖屏；登录页（设备名 + PIN）
- **电脑模式**：
  - 设备列表分组：已配对设备（电脑+手机）正常平铺；未配对设备折叠进"未配对设备 (N)"可展开区域；**若尚无任何已配对设备，未配对设备直接平铺**（方便首次配对）
  - 点击未配对电脑 → 弹 PIN 输入框 → 调 `POST /api/pair {peer_id, pin}`（本机服务代为向对方发起配对）
  - 侧栏显示本机 PIN（仅 localhost 可见）
  - 文件消息加下载按钮
- **登录视图**：内置于单页应用；前端任一 API 收到 401 即切换到登录视图，注册成功后回到聊天视图

## 7. 兼容性与迁移

- 升级后旧版本设备无法互传（缺 X-Pair-Token 被 403）——两边都升级并配对一次即可，历史记录不受影响（peer_id 不变）
- config.json 自动补 `pin` 字段；SQLite 自动建 `pairings`、`phones` 表（IF NOT EXISTS）

## 8. 范围外（明确不做）

- 手机经电脑中转发给第三方设备
- 手机后台接收、推送通知
- HTTPS / 证书（后果须知：PIN 与密钥在局域网内明文传输——配对机制防主动冒充，不防被动嗅探。可信家庭网络下接受；README 中向用户说明）
- staging 中发给手机的文件自动清理（永久保留，README 说明）
- 取消配对的管理界面（删 SQLite 记录即可，留待后续）

## 9. 测试策略

- 单测：PIN 生成与持久化、pairings/phones 表读写、配对握手纯逻辑、越权判定（手机 token 访问他人会话 → 拒绝）、防爆破计数器
- 集成（curl 双实例）：未配对互发 → 403；配对 → 双向收发成功；模拟手机注册 → 发文字/传文件/下载全链路；错误 PIN → 延迟 + 锁定
- 手动：真机 iPhone/安卓走一遍（注册、收发、下载、锁屏重连）

## 10. 里程碑

1. **M1**：配对基础——PIN 生成、pairings 表、/peer/pair 握手、X-Pair-Token 校验、防爆破
2. **M2**：鉴权边界——pre-routing 拦截、phones 表、手机注册/登录页、Cookie 校验
3. **M3**：手机会话——phone send-text/send-file/heartbeat、电脑→手机分支、/api/file 下载、越权防护
4. **M4**：前端——手机模式（单会话、翻转、响应式、登录页）、电脑端设备分组折叠、PIN 弹框、下载按钮
5. **M5**：回归与 README 更新
