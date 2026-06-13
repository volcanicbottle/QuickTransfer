#pragma once
#include <filesystem>

// 运行态文件 run.json：记录当前实例的实际监听端口，供 `--stop` 自调用。
namespace runstate {
void write(const std::filesystem::path& file, int port);
int read_port(const std::filesystem::path& file);  // 缺失/损坏返回 0
void remove(const std::filesystem::path& file);
}
