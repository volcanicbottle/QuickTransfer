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
