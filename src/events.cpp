#include "events.h"
#include <algorithm>

EventBus::SubPtr EventBus::subscribe() {
  auto s = std::make_shared<Subscriber>();
  std::lock_guard<std::mutex> lk(mu_);
  subs_.push_back(s);
  return s;
}

void EventBus::unsubscribe(const SubPtr& s) {
  std::lock_guard<std::mutex> lk(mu_);
  subs_.erase(std::remove(subs_.begin(), subs_.end(), s), subs_.end());
}

void EventBus::publish(const std::string& payload) {
  std::lock_guard<std::mutex> lk(mu_);
  for (auto& s : subs_) {
    std::lock_guard<std::mutex> lk2(s->mu);
    // 订阅者假死（浏览器断开未检测到）时丢弃最旧事件，防止无限堆积；
    // 前端重连后会整体重新拉取，丢事件无害
    if (s->queue.size() >= kMaxQueue) s->queue.pop_front();
    s->queue.push_back(payload);
    s->cv.notify_one();
  }
}
