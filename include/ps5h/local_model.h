#pragma once

#include <pthread.h>

#include <atomic>
#include <mutex>
#include <string>

namespace ps5h {

// Starts llama.cpp's OpenAI-compatible server inside this process. The local
// model is part of PS Harness: no separate PS5LM install, no phone, no laptop.
// The server binds loopback only, so the existing narrow local endpoint policy
// covers the client side.
class LocalModelRuntime {
 public:
  LocalModelRuntime() = default;
  ~LocalModelRuntime();
  LocalModelRuntime(const LocalModelRuntime&) = delete;
  LocalModelRuntime& operator=(const LocalModelRuntime&) = delete;

  bool start(const std::string& model_path, int port, int threads, int context,
             std::string* error);
  bool ready() const { return ready_.load(); }
  void stop();

 private:
  static void* run(void* context);

  pthread_t thread_{};
  bool thread_started_ = false;
  std::atomic<bool> ready_{false};
  std::atomic<bool> finished_{false};
  mutable std::mutex mutex_;
  std::string failure_;
};

}  // namespace ps5h
