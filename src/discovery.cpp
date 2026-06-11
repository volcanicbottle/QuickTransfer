#include "discovery.h"
#include "json.hpp"
#include "util.h"
#include <chrono>
#include <cstring>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
using socklen_t = int;
static void sock_init() { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); }
static void sock_close(sock_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
static void sock_init() {}
static void sock_close(sock_t s) { close(s); }
#endif

namespace discovery {

std::string make_announce(const std::string& id, const std::string& name, int port) {
  return nlohmann::json{{"app", "duoduan"}, {"id", id}, {"name", name}, {"port", port}}
      .dump();
}

bool parse_announce(const std::string& packet, PeerInfo& out) {
  auto j = nlohmann::json::parse(packet, nullptr, false);
  if (j.is_discarded() || j.value("app", "") != "duoduan") return false;
  out.id = j.value("id", "");
  out.name = j.value("name", "");
  out.port = j.value("port", 0);
  return !out.id.empty() && out.port > 0;
}

}  // namespace discovery

Discovery::Discovery(std::string self_id, std::string self_name, int http_port)
    : self_id_(std::move(self_id)),
      self_name_(std::move(self_name)),
      http_port_(http_port) {}

Discovery::~Discovery() { stop(); }

void Discovery::start(std::function<void()> on_change) {
  on_change_ = std::move(on_change);
  sock_init();
  running_ = true;
  bth_ = std::thread([this] { broadcast_loop(); });
  lth_ = std::thread([this] { listen_loop(); });
}

void Discovery::stop() {
  if (!running_) return;
  running_ = false;
  if (bth_.joinable()) bth_.join();
  if (lth_.joinable()) lth_.join();
}

void Discovery::broadcast_loop() {
  sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
  int yes = 1;
  setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof(yes));
  std::string msg = discovery::make_announce(self_id_, self_name_, http_port_);
  sockaddr_in bcast{};
  bcast.sin_family = AF_INET;
  bcast.sin_port = htons(discovery::kUdpPort);
  bcast.sin_addr.s_addr = INADDR_BROADCAST;
  sockaddr_in loop = bcast;
  loop.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 同机多实例测试用
  while (running_) {
    sendto(s, msg.data(), (int)msg.size(), 0, (sockaddr*)&bcast, sizeof(bcast));
    sendto(s, msg.data(), (int)msg.size(), 0, (sockaddr*)&loop, sizeof(loop));
    for (int i = 0; i < 30 && running_; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  sock_close(s);
}

void Discovery::listen_loop() {
  sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
  int yes = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
#ifdef SO_REUSEPORT
  setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (const char*)&yes, sizeof(yes));  // 同机多实例
#endif
#ifdef _WIN32
  DWORD tv = 1000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
  timeval tv{1, 0};
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(discovery::kUdpPort);
  addr.sin_addr.s_addr = INADDR_ANY;
  bind(s, (sockaddr*)&addr, sizeof(addr));
  char buf[1500];
  while (running_) {
    sockaddr_in from{};
    socklen_t flen = sizeof(from);
    int n = (int)recvfrom(s, buf, sizeof(buf), 0, (sockaddr*)&from, &flen);
    if (n <= 0) continue;
    PeerInfo p;
    if (!discovery::parse_announce(std::string(buf, n), p)) continue;
    if (p.id == self_id_) continue;
    char ipbuf[64] = {0};
    inet_ntop(AF_INET, &from.sin_addr, ipbuf, sizeof(ipbuf));
    p.ip = ipbuf;
    p.last_seen = util::now_ms();
    bool changed = false;
    {
      std::lock_guard<std::mutex> lk(mu_);
      auto it = peers_.find(p.id);
      changed = it == peers_.end() ||
                util::now_ms() - it->second.last_seen > discovery::kOfflineMs ||
                it->second.name != p.name || it->second.ip != p.ip ||
                it->second.port != p.port;
      peers_[p.id] = p;
    }
    if (changed && on_change_) on_change_();
  }
  sock_close(s);
}

std::vector<PeerInfo> Discovery::peers() const {
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<PeerInfo> v;
  for (auto& kv : peers_) v.push_back(kv.second);
  return v;
}

bool Discovery::find(const std::string& id, PeerInfo& out) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = peers_.find(id);
  if (it == peers_.end()) return false;
  out = it->second;
  return true;
}
