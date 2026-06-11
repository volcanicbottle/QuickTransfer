#pragma once
#include <filesystem>
#include <memory>
#include "auth.h"
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
  void setup_auth_routes();  // 配对与手机路由（定义在 server_auth.cpp）
  static bool is_local(const httplib::Request& req);  // 来源是否 127.0.0.1/::1
  bool phone_from_request(const httplib::Request& req, PhoneInfo& out) const;
  bool check_pair_token(const std::string& from_id, const httplib::Request& req) const;
  void publish_message(const char* type, const Message& m);
  void send_text(const PeerInfo& peer, const Message& m);  // 同步，更新状态
  void start_file_send(long long msg_id);                  // 后台线程推送文件

  Config cfg_;
  std::filesystem::path web_dir_;
  History history_;
  AuthStore auth_;
  PinGuard pin_guard_;
  EventBus bus_;
  std::unique_ptr<Discovery> discovery_;
  httplib::Server svr_;
  int port_ = 0;
};
