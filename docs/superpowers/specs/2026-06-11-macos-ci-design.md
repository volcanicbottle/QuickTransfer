# macOS 支持与三平台 CI 设计文档

日期：2026-06-11
状态：已确认

## 目标

在没有 Mac 实机的条件下验证并保证 macOS 构建可用；同时补上从未实测的 Windows (MSVC) 构建验证。

## 方案

1. **`.github/workflows/ci.yml`**：push/PR 触发，矩阵 ubuntu-latest / macos-latest / windows-latest，各自 `cmake -B build && cmake --build --config Release` 并运行全部单测（Windows 二进制在 build/Release/）。fail-fast 关闭，三平台独立出结果。
2. **平台修补**：CI 暴露什么修什么（macOS clang 与 Windows MSVC 首次编译）。仓库公开，匿名 API 轮询 run/job 结论自行迭代；需要错误详情时请用户帮忙贴 Actions 页面的红字。
3. **README**：加 macOS 构建一节（brew install cmake，命令与 Linux 相同），并加 CI 徽章。

## 范围外

- .dmg 安装包 / 代码签名公证（需付费开发者账号）
- 多架构二进制分发（用户本机编译天然匹配架构）
- 真机 macOS 功能验收（无 Mac，编译+单测绿即为本阶段验收标准）

## 验收

三平台 CI 全绿（编译成功 + 单测全过）。
