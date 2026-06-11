#include "auth.h"
#include <sqlite3.h>
#include <stdexcept>

static void exec_sql(sqlite3* db, const char* sql) {
  char* err = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    std::string e = err ? err : "sqlite 错误";
    sqlite3_free(err);
    throw std::runtime_error(e);
  }
}

AuthStore::AuthStore(const std::filesystem::path& db_file) {
  if (sqlite3_open(db_file.u8string().c_str(), &db_) != SQLITE_OK)
    throw std::runtime_error("无法打开数据库: " + db_file.string());
  exec_sql(db_, "PRAGMA journal_mode=WAL;");
  exec_sql(db_, "PRAGMA busy_timeout=3000;");
  exec_sql(db_, R"sql(
CREATE TABLE IF NOT EXISTS pairings(
  peer_id TEXT PRIMARY KEY,
  secret TEXT NOT NULL,
  name TEXT NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS phones(
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  token TEXT NOT NULL,
  last_seen INTEGER NOT NULL DEFAULT 0
);
)sql");
}

AuthStore::~AuthStore() {
  if (db_) sqlite3_close(db_);
}

void AuthStore::save_pairing(const std::string& peer_id, const std::string& secret,
                             const std::string& name) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_,
                     "INSERT OR REPLACE INTO pairings(peer_id,secret,name) VALUES(?,?,?)",
                     -1, &st, nullptr);
  sqlite3_bind_text(st, 1, peer_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, secret.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(st);
  sqlite3_finalize(st);
}

bool AuthStore::get_pairing_secret(const std::string& peer_id, std::string& out) const {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_, "SELECT secret FROM pairings WHERE peer_id=?", -1, &st, nullptr);
  sqlite3_bind_text(st, 1, peer_id.c_str(), -1, SQLITE_TRANSIENT);
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW) {
    const unsigned char* t = sqlite3_column_text(st, 0);
    out = t ? (const char*)t : "";
    found = true;
  }
  sqlite3_finalize(st);
  return found;
}

void AuthStore::remove_pairing(const std::string& peer_id) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_, "DELETE FROM pairings WHERE peer_id=?", -1, &st, nullptr);
  sqlite3_bind_text(st, 1, peer_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(st);
  sqlite3_finalize(st);
}

static PhoneInfo row_to_phone(sqlite3_stmt* st) {
  auto txt = [&](int col) {
    const unsigned char* t = sqlite3_column_text(st, col);
    return t ? std::string((const char*)t) : std::string();
  };
  PhoneInfo p;
  p.id = txt(0);
  p.name = txt(1);
  p.token = txt(2);
  p.last_seen = sqlite3_column_int64(st, 3);
  return p;
}

void AuthStore::save_phone(const PhoneInfo& p) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_,
                     "INSERT OR REPLACE INTO phones(id,name,token,last_seen) VALUES(?,?,?,?)",
                     -1, &st, nullptr);
  sqlite3_bind_text(st, 1, p.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, p.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, p.token.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 4, p.last_seen);
  sqlite3_step(st);
  sqlite3_finalize(st);
}

static bool find_phone(sqlite3* db, const char* sql, const std::string& key, PhoneInfo& out) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db, sql, -1, &st, nullptr);
  sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW) {
    out = row_to_phone(st);
    found = true;
  }
  sqlite3_finalize(st);
  return found;
}

bool AuthStore::find_phone_by_token(const std::string& token, PhoneInfo& out) const {
  return find_phone(db_, "SELECT id,name,token,last_seen FROM phones WHERE token=?", token, out);
}

bool AuthStore::find_phone_by_id(const std::string& id, PhoneInfo& out) const {
  return find_phone(db_, "SELECT id,name,token,last_seen FROM phones WHERE id=?", id, out);
}

void AuthStore::touch_phone(const std::string& id, long long now_ms) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_, "UPDATE phones SET last_seen=? WHERE id=?", -1, &st, nullptr);
  sqlite3_bind_int64(st, 1, now_ms);
  sqlite3_bind_text(st, 2, id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(st);
  sqlite3_finalize(st);
}

std::vector<PhoneInfo> AuthStore::phones() const {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_, "SELECT id,name,token,last_seen FROM phones ORDER BY name", -1, &st,
                     nullptr);
  std::vector<PhoneInfo> v;
  while (sqlite3_step(st) == SQLITE_ROW) v.push_back(row_to_phone(st));
  sqlite3_finalize(st);
  return v;
}

bool PinGuard::locked(long long now) {
  std::lock_guard<std::mutex> lk(mu_);
  return now < locked_until_;
}

bool PinGuard::throttled(long long now) {
  std::lock_guard<std::mutex> lk(mu_);
  return now < next_allowed_;
}

void PinGuard::record_failure(long long now) {
  std::lock_guard<std::mutex> lk(mu_);
  if (now < locked_until_) return;  // 锁定期间的失败不计数、不延长锁定
  next_allowed_ = now + kFailDelayMs;
  if (++failures_ >= kMaxFailures) {
    locked_until_ = now + kLockMs;
    failures_ = 0;
  }
}

void PinGuard::record_success() {
  std::lock_guard<std::mutex> lk(mu_);
  failures_ = 0;
}
