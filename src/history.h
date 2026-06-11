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
