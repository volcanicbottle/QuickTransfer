#pragma once
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

struct PhoneInfo {
  std::string id, name, token;
  long long last_seen = 0;
  std::string device = "phone";  // "phone" | "pc"
};

// 设备类型归一化：仅 "phone"/"pc"，其余一律按 "phone"
std::string normalize_device(const std::string& d);

// 配对与凭证存储：pairings（电脑互信密钥）+ phones（手机注册）
// 与 History 共用同一 db 文件（独立连接，WAL + busy_timeout）
class AuthStore {
 public:
  explicit AuthStore(const std::filesystem::path& db_file);
  ~AuthStore();
  AuthStore(const AuthStore&) = delete;
  AuthStore& operator=(const AuthStore&) = delete;

  void save_pairing(const std::string& peer_id, const std::string& secret,
                    const std::string& name);
  bool get_pairing_secret(const std::string& peer_id, std::string& out) const;
  void remove_pairing(const std::string& peer_id);

  void save_phone(const PhoneInfo& p);  // INSERT OR REPLACE
  bool find_phone_by_token(const std::string& token, PhoneInfo& out) const;
  bool find_phone_by_id(const std::string& id, PhoneInfo& out) const;
  void touch_phone(const std::string& id, long long now_ms);
  std::vector<PhoneInfo> phones() const;

 private:
  sqlite3* db_ = nullptr;
};

// PIN 防爆破：连续失败 kMaxFailures 次锁定 kLockMs；每次失败后 kFailDelayMs 内
// 直接拒绝（替代阻塞 worker 线程的 sleep）。时间由调用方传入便于测试。
class PinGuard {
 public:
  static constexpr int kMaxFailures = 10;
  static constexpr long long kLockMs = 300000;    // 5 分钟
  static constexpr long long kFailDelayMs = 1000; // 失败后的节流窗口

  bool locked(long long now);     // 处于 10 次锁定期
  bool throttled(long long now);  // 处于单次失败后的节流窗口
  void record_failure(long long now);
  void record_success();

 private:
  std::mutex mu_;
  int failures_ = 0;
  long long locked_until_ = 0;
  long long next_allowed_ = 0;
};
