#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct PeerInfo {
  std::string id, name, ip;
  int port = 0;
  long long last_seen = 0;  // util::now_ms() 时间戳
};

namespace discovery {
// 纯函数（可单测）
std::string make_announce(const std::string& id, const std::string& name, int port);
bool parse_announce(const std::string& packet, PeerInfo& out);  // 不填 ip/last_seen

constexpr int kUdpPort = 38520;
constexpr long long kOfflineMs = 10000;
}

// UDP 广播发现：每3秒广播一次自己，监听线程维护设备表
class Discovery {
 public:
  Discovery(std::string self_id, std::string self_name, int http_port);
  ~Discovery();
  void start(std::function<void()> on_change);  // 设备上线/信息变化时回调
  void stop();
  std::vector<PeerInfo> peers() const;
  bool find(const std::string& id, PeerInfo& out) const;

 private:
  void broadcast_loop();
  void listen_loop();

  std::string self_id_, self_name_;
  int http_port_;
  std::function<void()> on_change_;
  std::atomic<bool> running_{false};
  std::thread bth_, lth_;
  mutable std::mutex mu_;
  std::map<std::string, PeerInfo> peers_;
};
