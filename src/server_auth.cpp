#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include "json.hpp"
#include "server.h"
#include "util.h"

using nlohmann::json;

bool App::is_local(const httplib::Request& req) {
  return req.remote_addr == "127.0.0.1" || req.remote_addr == "::1";
}

bool App::phone_from_request(const httplib::Request& req, PhoneInfo& out) const {
  std::string token = util::get_cookie_value(req.get_header_value("Cookie"), "qt_token");
  return !token.empty() && auth_.find_phone_by_token(token, out);
}

bool App::check_pair_token(const std::string& from_id, const httplib::Request& req) const {
  std::string secret;
  if (!auth_.get_pairing_secret(from_id, secret)) return false;
  return !secret.empty() && req.get_header_value("X-Pair-Token") == secret;
}

void App::setup_auth_routes() {
  // 其他电脑 → 配对请求（凭本机 PIN 换共享密钥）
  svr_.Post("/peer/pair", [this](const httplib::Request& req, httplib::Response& res) {
    long long now = util::now_ms();
    if (pin_guard_.locked(now) || pin_guard_.throttled(now)) { res.status = 429; return; }
    auto j = json::parse(req.body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { res.status = 400; return; }
    std::string from_id, from_name, pin;
    try {
      from_id = j.value("from_id", "");
      from_name = j.value("from_name", "");
      pin = j.value("pin", "");
    } catch (const nlohmann::json::exception&) { res.status = 400; return; }
    if (from_id.empty()) { res.status = 400; return; }
    if (pin != cfg_.pin) {
      pin_guard_.record_failure(now);  // 之后 1 秒内一律 429（非阻塞节流，不占用 worker 线程）
      res.status = 403;
      return;
    }
    pin_guard_.record_success();
    std::string secret = util::gen_secret();
    auth_.save_pairing(from_id, secret, from_name);
    bus_.publish(json{{"type", "peers"}}.dump());
    json out{{"secret", secret}, {"id", cfg_.id}, {"name", cfg_.name}};
    res.set_content(out.dump(), "application/json");
  });

  // 手机首次注册：设备名 + PIN → 长期 Cookie
  svr_.Post("/api/phone/register", [this](const httplib::Request& req, httplib::Response& res) {
    long long now = util::now_ms();
    if (pin_guard_.locked(now) || pin_guard_.throttled(now)) { res.status = 429; return; }
    auto j = json::parse(req.body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { res.status = 400; return; }
    std::string phone_id, name, pin;
    try {
      phone_id = j.value("phone_id", "");
      name = j.value("name", "");
      pin = j.value("pin", "");
    } catch (const nlohmann::json::exception&) { res.status = 400; return; }
    if (phone_id.size() < 8 || phone_id.size() > 64 || name.empty() || name.size() > 128) {
      res.status = 400;
      return;
    }
    if (pin != cfg_.pin) {
      pin_guard_.record_failure(now);  // 之后 1 秒内一律 429
      res.status = 403;
      return;
    }
    pin_guard_.record_success();
    PhoneInfo p;
    p.id = phone_id;
    p.name = name;
    p.token = util::gen_secret();
    p.last_seen = now;
    auth_.save_phone(p);
    res.set_header("Set-Cookie", "qt_token=" + p.token +
                                     "; Max-Age=31536000; Path=/; HttpOnly; SameSite=Lax");
    bus_.publish(json{{"type", "peers"}}.dump());
    res.set_content("{}", "application/json");
  });

  // 本机浏览器 → 向目标设备发起配对（仅 localhost）
  svr_.Post("/api/pair", [this](const httplib::Request& req, httplib::Response& res) {
    if (!is_local(req)) { res.status = 403; return; }
    auto j = json::parse(req.body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { res.status = 400; return; }
    std::string peer_id, pin;
    try {
      peer_id = j.value("peer_id", "");
      pin = j.value("pin", "");
    } catch (const nlohmann::json::exception&) { res.status = 400; return; }
    PeerInfo peer;
    if (pin.empty() || !discovery_->find(peer_id, peer)) { res.status = 400; return; }
    httplib::Client cli(peer.ip, peer.port);
    cli.set_connection_timeout(3);
    cli.set_read_timeout(5);  // 对方错误 PIN 会延迟 1 秒响应
    cli.set_write_timeout(3);
    json body{{"from_id", cfg_.id}, {"from_name", cfg_.name}, {"pin", pin}};
    auto r = cli.Post("/peer/pair", body.dump(), "application/json");
    if (!r) { res.status = 502; return; }
    if (r->status != 200) { res.status = r->status; return; }  // 403=PIN错 429=锁定
    auto rj = json::parse(r->body, nullptr, false);
    std::string secret;
    if (!rj.is_discarded() && rj.is_object()) {
      try { secret = rj.value("secret", ""); } catch (const nlohmann::json::exception&) {}
    }
    if (secret.empty()) { res.status = 502; return; }
    auth_.save_pairing(peer_id, secret, peer.name);
    bus_.publish(json{{"type", "peers"}}.dump());
    res.set_content("{}", "application/json");
  });

  // 手机 → 发文字（写入本机即完成）
  svr_.Post("/api/phone/send-text", [this](const httplib::Request& req, httplib::Response& res) {
    PhoneInfo ph;
    if (!phone_from_request(req, ph)) { res.status = 401; return; }
    auto j = json::parse(req.body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { res.status = 400; return; }
    std::string text;
    try { text = j.value("text", ""); } catch (const nlohmann::json::exception&) {
      res.status = 400;
      return;
    }
    if (text.empty()) { res.status = 400; return; }
    Message m;
    m.peer_id = ph.id;
    m.direction = "in";  // 电脑视角：收到
    m.kind = "text";
    m.body = text;
    m.status = "ok";
    history_.add(m);
    auth_.touch_phone(ph.id, util::now_ms());
    publish_message("message", m);
    res.set_content(to_json(m).dump(), "application/json");
  });

  // 手机 → 上传文件（直接存下载目录）
  svr_.Post("/api/phone/send-file",
            [this](const httplib::Request& req, httplib::Response& res,
                   const httplib::ContentReader& reader) {
    namespace fs = std::filesystem;
    PhoneInfo ph;
    if (!phone_from_request(req, ph)) { res.status = 401; return; }
    fs::create_directories(cfg_.download_dir);
    std::string filename;
    fs::path part;
    auto ofs = std::make_shared<std::ofstream>();
    bool skipping = false;  // 只收第一个 file part（与 /api/send-file 同惯例）
    bool complete = reader(
        [&](const httplib::MultipartFormData& file) {
          if (ofs->is_open() || !filename.empty()) {
            skipping = true;
            return true;
          }
          filename = fs::path(file.filename).filename().string();
          if (filename.empty()) filename = "未命名";
          part = util::unique_path(cfg_.download_dir, filename + ".part");
          ofs->open(part, std::ios::binary);
          return ofs->good();
        },
        [&](const char* data, size_t len) {
          if (skipping) return true;
          ofs->write(data, (std::streamsize)len);
          return ofs->good();
        });
    if (ofs->is_open()) ofs->close();
    std::error_code ec;
    if (!complete || filename.empty() || !fs::exists(part)) {
      if (!part.empty()) fs::remove(part, ec);  // 中断/失败：清理半截文件
      res.status = 400;
      return;
    }
    fs::path target = util::unique_path(cfg_.download_dir, filename);
    fs::rename(part, target, ec);
    if (ec) {
      fs::remove(part, ec);
      res.status = 500;
      return;
    }
    Message m;
    m.peer_id = ph.id;
    m.direction = "in";
    m.kind = "file";
    m.file_name = target.filename().string();
    m.file_size = (long long)fs::file_size(target, ec);
    m.file_path = target.string();
    m.status = "ok";
    history_.add(m);
    auth_.touch_phone(ph.id, util::now_ms());
    publish_message("message", m);
    res.set_content(to_json(m).dump(), "application/json");
  });

  // 手机心跳（在线状态）
  svr_.Post("/api/phone/heartbeat", [this](const httplib::Request& req, httplib::Response& res) {
    PhoneInfo ph;
    if (!phone_from_request(req, ph)) { res.status = 401; return; }
    auth_.touch_phone(ph.id, util::now_ms());
    res.set_content("{}", "application/json");
  });

  // 下载消息附件：localhost 任意；手机仅限自己会话的消息
  svr_.Get("/api/file", [this](const httplib::Request& req, httplib::Response& res) {
    namespace fs = std::filesystem;
    long long id = std::atoll(req.get_param_value("id").c_str());
    Message m = history_.get(id);
    if (m.id == 0 || m.kind != "file" || m.file_path.empty()) { res.status = 404; return; }
    if (!is_local(req)) {
      PhoneInfo ph;
      if (!phone_from_request(req, ph) || m.peer_id != ph.id) { res.status = 403; return; }
    }
    auto file = std::make_shared<std::ifstream>(fs::path(m.file_path), std::ios::binary);
    std::error_code ec;
    auto size = fs::file_size(fs::path(m.file_path), ec);
    if (!*file || ec) { res.status = 404; return; }
    res.set_header("Content-Disposition",
                   "attachment; filename=\"download\"; filename*=UTF-8''" +
                       util::url_encode(m.file_name));
    res.set_content_provider(
        (size_t)size, "application/octet-stream",
        [file](size_t offset, size_t length, httplib::DataSink& sink) {
          file->seekg((std::streamoff)offset);
          char buf[65536];
          size_t want = std::min(length, sizeof(buf));
          file->read(buf, (std::streamsize)want);
          std::streamsize n = file->gcount();
          if (n <= 0) return false;
          return sink.write(buf, (size_t)n);
        });
  });
}
