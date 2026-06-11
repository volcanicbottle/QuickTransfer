#include "server.h"
#include <cstdio>
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
}

void App::send_text(const PeerInfo&, const Message&) {}  // 任务 9 实现
void App::start_file_send(long long) {}                  // 任务 11 实现
