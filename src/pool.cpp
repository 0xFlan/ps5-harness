#include "ps5h/pool.h"

#include <algorithm>
#include <atomic>
#include <thread>

namespace ps5h {

std::vector<ToolOutcome> ToolExecutor::run(const std::vector<ToolJob>& jobs) const {
  std::vector<ToolOutcome> outcomes(jobs.size());
  if (jobs.empty()) {
    return outcomes;
  }

  std::atomic<std::size_t> next{0};
  const int thread_count = std::min<int>(workers_, static_cast<int>(jobs.size()));
  std::vector<std::thread> threads;
  threads.reserve(static_cast<std::size_t>(thread_count));
  for (int i = 0; i < thread_count; ++i) {
    threads.emplace_back([&jobs, &outcomes, &next]() {
      for (;;) {
        const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
        if (index >= jobs.size()) {
          return;
        }
        const ToolJob& job = jobs[index];
        ToolOutcome& outcome = outcomes[index];
        outcome.index = job.index;
        outcome.result =
            run_with_deadline(job.handler, job.arguments, job.timeout_ms, &outcome.error);
      }
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
  return outcomes;
}

}  // namespace ps5h
