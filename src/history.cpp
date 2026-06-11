#include "history.h"
#include <sqlite3.h>
#include <algorithm>
#include <stdexcept>
#include "util.h"

nlohmann::json to_json(const Message& m) {
  return {{"id", m.id},           {"peer_id", m.peer_id},
          {"direction", m.direction}, {"kind", m.kind},
          {"body", m.body},       {"file_name", m.file_name},
          {"file_size", m.file_size}, {"file_path", m.file_path},
          {"status", m.status},   {"ts", m.ts}};
}

static void exec_sql(sqlite3* db, const char* sql) {
  char* err = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    std::string e = err ? err : "sqlite 错误";
    sqlite3_free(err);
    throw std::runtime_error(e);
  }
}

History::History(const std::filesystem::path& db_file) {
  if (sqlite3_open(db_file.u8string().c_str(), &db_) != SQLITE_OK)
    throw std::runtime_error("无法打开数据库: " + db_file.string());
  exec_sql(db_, "PRAGMA journal_mode=WAL;");
  exec_sql(db_, "PRAGMA busy_timeout=3000;");  // 同一 db 文件现在有 History/AuthStore 两个连接
  exec_sql(db_, R"sql(
CREATE TABLE IF NOT EXISTS messages(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  peer_id TEXT NOT NULL,
  direction TEXT NOT NULL,
  kind TEXT NOT NULL,
  body TEXT NOT NULL DEFAULT '',
  file_name TEXT NOT NULL DEFAULT '',
  file_size INTEGER NOT NULL DEFAULT 0,
  file_path TEXT NOT NULL DEFAULT '',
  status TEXT NOT NULL DEFAULT 'ok',
  ts INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_peer ON messages(peer_id, id);
)sql");
}

History::~History() {
  if (db_) sqlite3_close(db_);
}

static Message row_to_message(sqlite3_stmt* st) {
  auto txt = [&](int col) {
    const unsigned char* t = sqlite3_column_text(st, col);
    return t ? std::string((const char*)t) : std::string();
  };
  Message m;
  m.id = sqlite3_column_int64(st, 0);
  m.peer_id = txt(1);
  m.direction = txt(2);
  m.kind = txt(3);
  m.body = txt(4);
  m.file_name = txt(5);
  m.file_size = sqlite3_column_int64(st, 6);
  m.file_path = txt(7);
  m.status = txt(8);
  m.ts = sqlite3_column_int64(st, 9);
  return m;
}

long long History::add(Message& m) {
  if (m.ts == 0) m.ts = util::now_ms();
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_,
                     "INSERT INTO messages(peer_id,direction,kind,body,file_name,"
                     "file_size,file_path,status,ts) VALUES(?,?,?,?,?,?,?,?,?)",
                     -1, &st, nullptr);
  sqlite3_bind_text(st, 1, m.peer_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, m.direction.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, m.kind.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, m.body.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 5, m.file_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 6, m.file_size);
  sqlite3_bind_text(st, 7, m.file_path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 8, m.status.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 9, m.ts);
  sqlite3_step(st);
  sqlite3_finalize(st);
  m.id = sqlite3_last_insert_rowid(db_);
  return m.id;
}

std::vector<Message> History::list(const std::string& peer_id, int limit) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_,
                     "SELECT id,peer_id,direction,kind,body,file_name,file_size,"
                     "file_path,status,ts FROM messages WHERE peer_id=? "
                     "ORDER BY id DESC LIMIT ?",
                     -1, &st, nullptr);
  sqlite3_bind_text(st, 1, peer_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 2, limit);
  std::vector<Message> v;
  while (sqlite3_step(st) == SQLITE_ROW) v.push_back(row_to_message(st));
  sqlite3_finalize(st);
  std::reverse(v.begin(), v.end());  // 升序返回
  return v;
}

void History::set_status(long long id, const std::string& status) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_, "UPDATE messages SET status=? WHERE id=?", -1, &st, nullptr);
  sqlite3_bind_text(st, 1, status.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 2, id);
  sqlite3_step(st);
  sqlite3_finalize(st);
}

void History::set_file_path(long long id, const std::string& path) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_, "UPDATE messages SET file_path=? WHERE id=?", -1, &st, nullptr);
  sqlite3_bind_text(st, 1, path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 2, id);
  sqlite3_step(st);
  sqlite3_finalize(st);
}

Message History::get(long long id) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db_,
                     "SELECT id,peer_id,direction,kind,body,file_name,file_size,"
                     "file_path,status,ts FROM messages WHERE id=?",
                     -1, &st, nullptr);
  sqlite3_bind_int64(st, 1, id);
  Message m;
  if (sqlite3_step(st) == SQLITE_ROW) m = row_to_message(st);
  sqlite3_finalize(st);
  return m;
}
