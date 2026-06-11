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

TEST_CASE("EventBus 队列有上界，超出丢弃最旧事件") {
  EventBus bus;
  auto a = bus.subscribe();
  for (size_t i = 0; i < EventBus::kMaxQueue + 10; ++i)
    bus.publish(std::to_string(i));
  std::lock_guard<std::mutex> lk(a->mu);
  CHECK(a->queue.size() == EventBus::kMaxQueue);
  CHECK(a->queue.back() == std::to_string(EventBus::kMaxQueue + 9));  // 最新的保留
  CHECK(a->queue.front() == "10");  // 最旧的 10 条被丢弃
}
