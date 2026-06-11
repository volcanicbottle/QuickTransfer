#pragma once
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// 简单的发布-订阅总线：HTTP SSE 处理器订阅，业务代码发布 JSON 字符串
class EventBus {
 public:
  struct Subscriber {
    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::string> queue;
  };
  using SubPtr = std::shared_ptr<Subscriber>;

  // 单个订阅者队列上限，超出丢弃最旧事件
  static constexpr size_t kMaxQueue = 1024;

  SubPtr subscribe();
  void unsubscribe(const SubPtr& s);
  void publish(const std::string& payload);

 private:
  std::mutex mu_;
  std::vector<SubPtr> subs_;
};
