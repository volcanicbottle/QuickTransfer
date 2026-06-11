#pragma once
#include <filesystem>
#include <memory>
#include "config.h"
#include "discovery.h"
#include "events.h"
#include "history.h"
#include "httplib.h"

class App {
 public:
  App(Config cfg, std::filesystem::path web_dir);
  bool run();  // 阻塞运行；绑定失败返回 false
  int port() const { return port_; }

 private:
  void setup_routes();
  void publish_message(const char* type, const Message& m);
  void send_text(const PeerInfo& peer, const Message& m);  // 同步，更新状态
  void start_file_send(long long msg_id);                  // 后台线程推送文件

  Config cfg_;
  std::filesystem::path web_dir_;
  History history_;
  EventBus bus_;
  std::unique_ptr<Discovery> discovery_;
  httplib::Server svr_;
  int port_ = 0;
};
