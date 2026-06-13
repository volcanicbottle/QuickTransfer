#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>
#include "config.h"
#include "httplib.h"
#include "runstate.h"
#include "server.h"
#include "util.h"

int main(int argc, char** argv) {
  namespace fs = std::filesystem;
  fs::path data_dir = util::home_dir() / ".duoduan";
  int port_override = 0;
  std::string name_override, download_override;
  bool stop_flag = false;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--stop")) stop_flag = true;
    if (i + 1 < argc) {
      if (!std::strcmp(argv[i], "--data-dir")) data_dir = argv[i + 1];
      if (!std::strcmp(argv[i], "--port")) port_override = std::atoi(argv[i + 1]);
      if (!std::strcmp(argv[i], "--name")) name_override = argv[i + 1];
      if (!std::strcmp(argv[i], "--download-dir")) download_override = argv[i + 1];
    }
  }

  if (stop_flag) {
    auto run_file = data_dir / "run.json";
    int port = runstate::read_port(run_file);
    if (!port) {
      std::printf("服务未在运行\n");
      runstate::remove(run_file);
      return 0;
    }
    httplib::Client cli("127.0.0.1", port);
    cli.set_connection_timeout(2);
    auto r = cli.Post("/api/quit", "{}", "application/json");
    if (!r) {
      std::printf("服务未在运行\n");
      runstate::remove(run_file);
      return 0;
    }
    for (int i = 0; i < 50; ++i) {  // 最多等 ~5s：正常情况端口拒绝连接即退出；服务挂起未释放端口则到期后无条件视为已停止
      httplib::Client probe("127.0.0.1", port);
      probe.set_connection_timeout(1);
      if (!probe.Get("/api/self")) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::printf("已停止\n");
    return 0;
  }

  Config cfg = Config::load(data_dir);
  if (port_override) cfg.port = port_override;
  if (!name_override.empty()) cfg.name = name_override;
  if (!download_override.empty()) cfg.download_dir = download_override;

  fs::path web_dir = "web";
  if (!fs::exists(web_dir / "index.html") && fs::exists("../web/index.html"))
    web_dir = "../web";  // 支持从 build/ 目录运行

  App app(std::move(cfg), web_dir);
  return app.run() ? 0 : 1;
}
