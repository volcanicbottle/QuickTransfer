#include "server.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <thread>
#include "json.hpp"
#include "util.h"

using nlohmann::json;

App::App(Config cfg, std::filesystem::path web_dir)
    : cfg_(std::move(cfg)),
      web_dir_(std::move(web_dir)),
      history_(cfg_.db_path()) {}

bool App::run() {
  namespace fs = std::filesystem;
  fs::create_directories(cfg_.download_dir);
  fs::create_directories(cfg_.staging_dir());

  port_ = cfg_.port;
  while (port_ < cfg_.port + 10 && !svr_.bind_to_port("0.0.0.0", port_)) port_++;
  if (port_ >= cfg_.port + 10) {
    std::printf("错误：端口 %d-%d 都被占用\n", cfg_.port, port_ - 1);
    return false;
  }

  discovery_ = std::make_unique<Discovery>(cfg_.id, cfg_.name, port_);
  discovery_->start([this] { bus_.publish(json{{"type", "peers"}}.dump()); });

  setup_routes();
  std::printf("已启动：http://localhost:%d  （设备名：%s）\n", port_, cfg_.name.c_str());
  return svr_.listen_after_bind();
}

void App::publish_message(const char* type, const Message& m) {
  bus_.publish(json{{"type", type}, {"message", to_json(m)}}.dump());
}

void App::setup_routes() {
  if (!svr_.set_mount_point("/", web_dir_.string()))
    std::fprintf(stderr, "警告：静态目录 %s 不存在，页面将不可用\n",
                 web_dir_.string().c_str());

  svr_.Get("/api/self", [this](const httplib::Request&, httplib::Response& res) {
    json j{{"id", cfg_.id}, {"name", cfg_.name}, {"port", port_}};
    res.set_content(j.dump(), "application/json");
  });

  svr_.Get("/api/peers", [this](const httplib::Request&, httplib::Response& res) {
    json arr = json::array();
    long long now = util::now_ms();
    for (auto& p : discovery_->peers()) {
      arr.push_back({{"id", p.id},
                     {"name", p.name},
                     {"ip", p.ip},
                     {"port", p.port},
                     {"online", now - p.last_seen < discovery::kOfflineMs}});
    }
    res.set_content(arr.dump(), "application/json");
  });

  svr_.Get("/api/events", [this](const httplib::Request&, httplib::Response& res) {
    auto sub = bus_.subscribe();
    res.set_header("Cache-Control", "no-cache");
    res.set_chunked_content_provider(
        "text/event-stream",
        [sub](size_t, httplib::DataSink& sink) {
          std::string chunk;
          {
            std::unique_lock<std::mutex> lk(sub->mu);
            sub->cv.wait_for(lk, std::chrono::seconds(5),
                             [&] { return !sub->queue.empty(); });
            if (sub->queue.empty()) {
              chunk = ": ping\n\n";  // 保活注释行
            } else {
              while (!sub->queue.empty()) {
                chunk += "data: " + sub->queue.front() + "\n\n";
                sub->queue.pop_front();
              }
            }
          }
          return sink.write(chunk.data(), chunk.size());
        },
        [this, sub](bool) { bus_.unsubscribe(sub); });
  });

  // 历史记录
  svr_.Get("/api/messages", [this](const httplib::Request& req, httplib::Response& res) {
    auto peer = req.get_param_value("peer");
    json arr = json::array();
    for (auto& m : history_.list(peer)) arr.push_back(to_json(m));
    res.set_content(arr.dump(), "application/json");
  });

  // 本机浏览器 → 发送文字
  svr_.Post("/api/send-text", [this](const httplib::Request& req, httplib::Response& res) {
    auto j = json::parse(req.body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { res.status = 400; return; }
    std::string peer_id, text;
    try {
      peer_id = j.value("peer_id", "");
      text = j.value("text", "");
    } catch (const nlohmann::json::exception&) { res.status = 400; return; }
    PeerInfo peer;
    if (text.empty() || !discovery_->find(peer_id, peer)) { res.status = 400; return; }
    Message m;
    m.peer_id = peer_id;
    m.direction = "out";
    m.kind = "text";
    m.body = text;
    m.status = "pending";
    history_.add(m);
    send_text(peer, m);
    m = history_.get(m.id);
    publish_message("message", m);
    res.set_content(to_json(m).dump(), "application/json");
  });

  // 其他电脑 → 接收文字
  svr_.Post("/peer/text", [this](const httplib::Request& req, httplib::Response& res) {
    auto j = json::parse(req.body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { res.status = 400; return; }
    Message m;
    try {
      m.peer_id = j.value("from_id", "");
      m.direction = "in";
      m.kind = "text";
      m.body = j.value("text", "");
      m.status = "ok";
    } catch (const nlohmann::json::exception&) { res.status = 400; return; }
    if (m.peer_id.empty() || m.body.empty()) { res.status = 400; return; }
    history_.add(m);
    publish_message("message", m);
    res.set_content("{}", "application/json");
  });

  // 其他电脑 → 接收文件（流式写 .part，完成后改名）
  svr_.Post("/peer/file", [this](const httplib::Request& req, httplib::Response& res,
                                 const httplib::ContentReader& reader) {
    namespace fs = std::filesystem;
    std::string name = req.get_param_value("name");
    std::string from_id = req.get_param_value("from_id");
    long long size = std::atoll(req.get_param_value("size").c_str());
    if (name.empty() || from_id.empty()) { res.status = 400; return; }
    // 文件名只取末段，防止路径穿越
    name = fs::path(name).filename().string();
    if (name.empty() || name == "." || name == "..") { res.status = 400; return; }
    fs::create_directories(cfg_.download_dir);
    fs::path part = util::unique_path(cfg_.download_dir, name + ".part");
    std::ofstream ofs(part, std::ios::binary);
    if (!ofs) { res.status = 500; return; }
    reader([&](const char* data, size_t len) {
      ofs.write(data, (std::streamsize)len);
      return ofs.good();
    });
    ofs.close();
    std::error_code ec;
    bool ok = fs::exists(part) && (size <= 0 || (long long)fs::file_size(part, ec) == size);
    if (!ok) {
      fs::remove(part, ec);
      res.status = 500;
      return;
    }
    fs::path final_path = util::unique_path(cfg_.download_dir, name);
    fs::rename(part, final_path, ec);
    if (ec) {
      fs::remove(part, ec);
      res.status = 500;
      return;
    }
    Message m;
    m.peer_id = from_id;
    m.direction = "in";
    m.kind = "file";
    m.file_name = final_path.filename().string();
    m.file_size = (long long)fs::file_size(final_path, ec);
    m.file_path = final_path.string();
    m.status = "ok";
    history_.add(m);
    publish_message("message", m);
    res.set_content("{}", "application/json");
  });

  // 本机浏览器 → 上传文件到暂存区并后台推送给目标设备
  svr_.Post("/api/send-file", [this](const httplib::Request& req, httplib::Response& res,
                                     const httplib::ContentReader& reader) {
    namespace fs = std::filesystem;
    std::string peer_id = req.get_param_value("peer");
    PeerInfo peer;
    if (!discovery_->find(peer_id, peer)) { res.status = 400; return; }
    fs::create_directories(cfg_.staging_dir());
    std::string filename;
    fs::path staged;
    auto ofs = std::make_shared<std::ofstream>();
    bool skipping = false;  // 多个 file part 时只收第一个，忽略其余
    reader(
        [&](const httplib::MultipartFormData& file) {
          if (ofs->is_open() || !filename.empty()) {
            skipping = true;
            return true;  // 忽略后续 part，不中止解析
          }
          filename = fs::path(file.filename).filename().string();
          if (filename.empty()) filename = "未命名";
          staged = util::unique_path(cfg_.staging_dir(), filename);
          ofs->open(staged, std::ios::binary);
          return ofs->good();
        },
        [&](const char* data, size_t len) {
          if (skipping) return true;  // 丢弃被忽略 part 的数据
          ofs->write(data, (std::streamsize)len);
          return ofs->good();
        });
    if (ofs->is_open()) ofs->close();
    std::error_code ec;
    if (filename.empty() || !fs::exists(staged)) {
      if (!staged.empty()) fs::remove(staged, ec);  // 清理半成品
      res.status = 400;
      return;
    }
    Message m;
    m.peer_id = peer_id;
    m.direction = "out";
    m.kind = "file";
    m.file_name = filename;
    m.file_size = (long long)fs::file_size(staged, ec);
    m.file_path = staged.string();
    m.status = "pending";
    history_.add(m);
    publish_message("message", m);
    start_file_send(m.id);
    res.set_content(to_json(m).dump(), "application/json");
  });

  // 重试发送失败的消息
  svr_.Post("/api/retry", [this](const httplib::Request& req, httplib::Response& res) {
    auto j = json::parse(req.body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { res.status = 400; return; }
    long long id = 0;
    try {
      id = j.value("message_id", (long long)0);
    } catch (const nlohmann::json::exception&) { res.status = 400; return; }
    Message m = history_.get(id);
    if (m.id == 0 || m.direction != "out" || m.status != "fail") {
      res.status = 400;
      return;
    }
    history_.set_status(m.id, "pending");
    publish_message("message_update", history_.get(m.id));
    if (m.kind == "text") {
      PeerInfo peer;
      if (discovery_->find(m.peer_id, peer)) {
        send_text(peer, m);
      } else {
        history_.set_status(m.id, "fail");
      }
      publish_message("message_update", history_.get(m.id));
    } else {
      start_file_send(m.id);  // 文件：暂存还在（失败时不删除）
    }
    res.set_content("{}", "application/json");
  });
}

void App::send_text(const PeerInfo& peer, const Message& m) {
  httplib::Client cli(peer.ip, peer.port);
  cli.set_connection_timeout(3);
  cli.set_read_timeout(3);
  cli.set_write_timeout(3);
  json j{{"from_id", cfg_.id}, {"from_name", cfg_.name}, {"text", m.body}};
  auto r = cli.Post("/peer/text", j.dump(), "application/json");
  history_.set_status(m.id, (r && r->status == 200) ? "ok" : "fail");
}
void App::start_file_send(long long msg_id) {
  std::thread([this, msg_id] {
    namespace fs = std::filesystem;
    Message m = history_.get(msg_id);
    PeerInfo peer;
    bool ok = false;
    if (m.id != 0 && discovery_->find(m.peer_id, peer) && fs::exists(m.file_path)) {
      httplib::Client cli(peer.ip, peer.port);
      cli.set_connection_timeout(3);
      cli.set_read_timeout(60);
      cli.set_write_timeout(30);
      auto file = std::make_shared<std::ifstream>(m.file_path, std::ios::binary);
      long long total = m.file_size;
      auto sent = std::make_shared<long long>(0);
      auto last_pub = std::make_shared<long long>(0);
      std::string path = "/peer/file?name=" + util::url_encode(m.file_name) +
                         "&from_id=" + util::url_encode(cfg_.id) +
                         "&from_name=" + util::url_encode(cfg_.name) +
                         "&size=" + std::to_string(total);
      auto r = cli.Post(
          path, httplib::Headers{}, (size_t)total,
          [this, file, sent, last_pub, total, msg_id](size_t, size_t length,
                                                      httplib::DataSink& sink) {
            char buf[65536];
            size_t want = std::min(length, sizeof(buf));
            file->read(buf, (std::streamsize)want);
            std::streamsize n = file->gcount();
            if (n <= 0) return false;
            if (!sink.write(buf, (size_t)n)) return false;
            *sent += n;
            long long now = util::now_ms();
            if (now - *last_pub > 200 || *sent >= total) {
              *last_pub = now;
              bus_.publish(json{{"type", "progress"},
                                {"message_id", msg_id},
                                {"sent", *sent},
                                {"total", total}}.dump());
            }
            return true;
          },
          "application/octet-stream");
      ok = r && r->status == 200;
    }
    history_.set_status(msg_id, ok ? "ok" : "fail");
    if (ok) {
      std::error_code ec;
      fs::remove(m.file_path, ec);          // 发送成功删除暂存
      history_.set_file_path(msg_id, "");   // 历史只留文件名和大小
    }
    publish_message("message_update", history_.get(msg_id));
  }).detach();
}
