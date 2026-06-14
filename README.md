# 多端互通

[![CI](https://github.com/volcanicbottle/QuickTransfer/actions/workflows/ci.yml/badge.svg)](https://github.com/volcanicbottle/QuickTransfer/actions/workflows/ci.yml)

类似微信文件传输助手的局域网互传工具：聊天式界面，支持文字消息与文件传输，
自动发现同一局域网内的设备，历史记录本地保存（SQLite）。

## 构建（Linux）

```bash
sudo apt install g++ cmake          # 如未安装
cmake -B build && cmake --build build -j
```

## 构建（macOS）

```bash
xcode-select --install               # 如未安装命令行工具
brew install cmake                   # 如未安装
cmake -B build && cmake --build build -j
```

首次运行时 macOS 会弹"是否允许接收传入网络连接"，选允许。

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

注意：首次运行时允许 Windows 防火墙的"专用网络"访问（程序需要 UDP 38520 端口做设备发现、
TCP 8520 端口提供服务），否则无法被发现。

## 运行

```bash
./build/duoduan          # 在仓库根目录运行（需要找到 web/ 目录）
```

浏览器打开 http://localhost:8520 （程序启动时会打印实际端口）。
两台电脑都启动后会自动互相发现，左侧列表点击对方设备即可开始传输。

### 停止服务

三种方式均可停止运行中的服务：

- 网页侧边栏点「⏻ 停止服务」按钮；
- 在运行服务的终端按 `Ctrl+C`；
- 另开终端执行 `./duoduan --stop`（程序会读取本机 `run.json` 记录的端口，自动发送停止请求）。

命令行参数：

| 参数 | 说明 | 默认 |
|---|---|---|
| `--port N` | HTTP 端口 | 8520（占用时自动+1） |
| `--name 名称` | 设备显示名 | 主机名 |
| `--download-dir 路径` | 接收文件保存目录 | ~/Downloads/多端互通 |
| `--data-dir 路径` | 配置与历史记录目录 | ~/.duoduan |

## 配对（v2 起必需）

出于安全考虑，设备之间必须配对一次才能互传：

- **电脑 ↔ 电脑**：在 A 的页面左侧"未配对设备"里点击 B，输入 B 屏幕上显示的
  6 位 PIN（启动时终端会打印，页面侧栏也显示）。配对一次双向有效。
- **手机扫码接入**：电脑端网页侧边栏点「📱 手机接入」按钮，弹出二维码；手机用
  相机或浏览器扫码打开（需与电脑处于同一 Wi-Fi/局域网），登录页会自动带入 PIN，
  只需填写设备名即可完成配对。
- **另一台电脑用浏览器接入**：无需安装本程序，在另一台电脑的浏览器中直接打开
  `http://<服务器IP>:8520/`，按提示输入运行服务的电脑屏幕上显示的 6 位 PIN 即可作
  为客户端使用。设备列表中 💻 表示电脑设备、📱 表示手机设备。

注意：手机锁屏或切走后传输会中断（浏览器限制），回到页面会自动重连并补全记录。
发给手机的文件保留在 `~/.duoduan/staging/`，手机可随时重新下载。

## 测试

```bash
./build/unit_tests
```

## 安全说明

- 局域网内未配对的设备只能"看见"你的设备名，无法发送文件或查看页面
- PIN 连续输错 10 次锁定 5 分钟
- 手机凭证仅能访问自己的会话，无法查看其他设备的聊天记录
- 限制：传输为明文 HTTP——配对可防主动冒充，**不防被动嗅探**（局域网内的
  抓包者可截获 PIN、密钥与传输内容）。请仅在可信网络（家庭 WiFi）使用

## 路线图

- [x] 手机浏览器接入（手机 ↔ 电脑会话）
- [x] 配对 PIN 码
- [ ] 取消配对/设备管理界面
