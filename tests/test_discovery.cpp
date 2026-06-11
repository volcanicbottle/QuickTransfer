#include "doctest.h"
#include "discovery.h"

TEST_CASE("announce 报文序列化与解析往返") {
  auto pkt = discovery::make_announce("abc123", "我的电脑", 8521);
  PeerInfo p;
  REQUIRE(discovery::parse_announce(pkt, p));
  CHECK(p.id == "abc123");
  CHECK(p.name == "我的电脑");
  CHECK(p.port == 8521);
}

TEST_CASE("parse_announce 拒绝非法报文") {
  PeerInfo p;
  CHECK_FALSE(discovery::parse_announce("not json", p));
  CHECK_FALSE(discovery::parse_announce(R"({"app":"other","id":"x","name":"n","port":1})", p));
  CHECK_FALSE(discovery::parse_announce(R"({"app":"duoduan","id":"","name":"n","port":1})", p));
  CHECK_FALSE(discovery::parse_announce(R"({"app":"duoduan","id":"x","name":"n","port":0})", p));
}
