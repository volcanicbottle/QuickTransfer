#include "doctest.h"
#include "events.h"

TEST_CASE("EventBus 发布到所有订阅者，退订后不再接收") {
  EventBus bus;
  auto a = bus.subscribe();
  auto b = bus.subscribe();
  bus.publish("hello");
  {
    std::lock_guard<std::mutex> lk(a->mu);
    REQUIRE(a->queue.size() == 1);
    CHECK(a->queue.front() == "hello");
  }
  bus.unsubscribe(a);
  bus.publish("again");
  {
    std::lock_guard<std::mutex> lk(a->mu);
    CHECK(a->queue.size() == 1);  // 没有新增
  }
  {
    std::lock_guard<std::mutex> lk(b->mu);
    CHECK(b->queue.size() == 2);
  }
}
