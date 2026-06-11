#include <cstdlib>
#include <cstring>
#include <filesystem>
#include "config.h"
#include "server.h"
#include "util.h"

int main(int argc, char** argv) {
  namespace fs = std::filesystem;
  fs::path data_dir = util::home_dir() / ".duoduan";
  int port_override = 0;
  std::string name_override, download_override;
  for (int i = 1; i + 1 < argc; ++i) {
    if (!std::strcmp(argv[i], "--data-dir")) data_dir = argv[i + 1];
    if (!std::strcmp(argv[i], "--port")) port_override = std::atoi(argv[i + 1]);
    if (!std::strcmp(argv[i], "--name")) name_override = argv[i + 1];
    if (!std::strcmp(argv[i], "--download-dir")) download_override = argv[i + 1];
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
