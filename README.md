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

注意：首次运行时允许 Windows 防火墙的"专用网络"访问（程序需要 UDP 38520 端口做设备发现、
TCP 8520 端口提供服务），否则无法被发现。

## 运行

```bash
./build/duoduan          # 在仓库根目录运行（需要找到 web/ 目录）
```

浏览器打开 http://localhost:8520 （程序启动时会打印实际端口）。
两台电脑都启动后会自动互相发现，左侧列表点击对方设备即可开始传输。

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
