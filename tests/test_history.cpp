#include "doctest.h"
#include "history.h"
#include "util.h"
#include <filesystem>

TEST_CASE("History 增/查/改状态") {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / ("dd_db_" + util::gen_id());
  fs::create_directories(dir);
  {
    History h(dir / "t.db");
    Message m;
    m.peer_id = "p1";
    m.direction = "out";
    m.kind = "text";
    m.body = "你好";
    m.status = "pending";
    h.add(m);
    CHECK(m.id > 0);
    CHECK(m.ts > 0);

    auto list = h.list("p1");
    REQUIRE(list.size() == 1);
    CHECK(list[0].body == "你好");
    CHECK(list[0].status == "pending");
    CHECK(h.list("p2").empty());

    h.set_status(m.id, "ok");
    CHECK(h.get(m.id).status == "ok");
    h.set_file_path(m.id, "");
    CHECK(h.get(m.id).file_path == "");
    CHECK(h.get(99999).id == 0);
  }
  fs::remove_all(dir);
}

TEST_CASE("History 文件消息字段完整保存") {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / ("dd_db2_" + util::gen_id());
  fs::create_directories(dir);
  {
    History h(dir / "t.db");
    Message m;
    m.peer_id = "p1";
    m.direction = "in";
    m.kind = "file";
    m.file_name = "照片.jpg";
    m.file_size = 12345;
    m.file_path = "/tmp/照片.jpg";
    m.status = "ok";
    h.add(m);
    auto got = h.get(m.id);
    CHECK(got.file_name == "照片.jpg");
    CHECK(got.file_size == 12345);
    CHECK(got.file_path == "/tmp/照片.jpg");
  }
  fs::remove_all(dir);
}
