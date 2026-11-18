// Runtime-scaling assertions. These use synthetic resource profiles so they
// fail when the worker count stops following online CPUs, when the fd limit is
// ignored, or when a memory or storage budget turns back into a fixed constant.

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "ps5h/json.h"
#include "ps5h/pool.h"
#include "ps5h/runtime.h"

namespace {

constexpr unsigned long long kMiB = 1024ULL * 1024ULL;

void check(bool condition, const char* what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s\n", what);
    std::exit(1);
  }
  std::printf("ok %s\n", what);
}

ps5h::ResourceProfile make_profile(int cpus, unsigned long long total_memory,
                                   unsigned long long available_memory, bool available_known,
                                   unsigned long long nofile_soft,
                                   unsigned long long file_size_limit = 0) {
  ps5h::ResourceProfile profile;
  profile.cpu_online = cpus;
  profile.cpu_configured = cpus;
  profile.memory_total_bytes = total_memory;
  profile.memory_available_bytes = available_memory;
  profile.memory_available_known = available_known;
  profile.limits.nofile_soft = nofile_soft;
  profile.limits.file_size_soft = file_size_limit;
  return profile;
}

struct Concurrency {
  std::atomic<int> active{0};
  std::atomic<int> peak{0};
};

ps5h::ToolJob make_job(std::size_t index, Concurrency* counter) {
  ps5h::ToolJob job;
  job.index = index;
  job.timeout_ms = 5000;
  job.handler = [counter](const ps5h::Json&) -> ps5h::Json {
    const int now = counter->active.fetch_add(1) + 1;
    int previous = counter->peak.load();
    while (now > previous && !counter->peak.compare_exchange_weak(previous, now)) {
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    counter->active.fetch_sub(1);
    return ps5h::Json("ok");
  };
  return job;
}

}  // namespace

int main() {
  using namespace ps5h;

  check(budget_from_profile(make_profile(8, 16 * 1024 * kMiB, 8 * 1024 * kMiB, true, 1024))
                .workers == 8,
        "workers equal online CPUs under a normal fd limit");
  check(budget_from_profile(make_profile(3, 16 * 1024 * kMiB, 8 * 1024 * kMiB, true, 1024))
                .workers == 3,
        "workers follow the CPU count down");
  check(budget_from_profile(make_profile(8, 16 * 1024 * kMiB, 8 * 1024 * kMiB, true, 24))
                .workers == 1,
        "a tight fd limit reduces workers instead of being ignored");
  check(budget_from_profile(make_profile(8, 16 * 1024 * kMiB, 8 * 1024 * kMiB, true, 64))
                .workers == 5,
        "the worker count accounts for descriptors per worker");
  check(budget_from_profile(make_profile(8, 16 * 1024 * kMiB, 8 * 1024 * kMiB, true, 0))
                .workers == 8,
        "an unlimited fd limit leaves the CPU count in charge");

  const RuntimeBudget large =
      budget_from_profile(make_profile(8, 16 * 1024 * kMiB, 8 * 1024 * kMiB, true, 1024));
  check(large.buffered_response_bytes > 4 * kMiB,
        "buffered responses grow past the old 4 MiB ceiling");
  check(large.buffered_response_bytes == 512 * kMiB,
        "buffered responses are one sixteenth of visible memory");

  const RuntimeBudget small =
      budget_from_profile(make_profile(2, 64 * kMiB, 32 * kMiB, true, 1024));
  check(small.buffered_response_bytes == 2 * kMiB,
        "a small memory envelope shrinks the buffered response budget");
  check(small.file_read_bytes <= 4 * kMiB, "file reads stay inside the context budget");
  check(small.workers == 2, "small memory does not silently serialize the CPU count");

  const RuntimeBudget fallback =
      budget_from_profile(make_profile(1, 0, 0, false, 0, 12345));
  check(fallback.buffered_response_bytes == 4 * kMiB,
        "an unmeasurable platform gets the documented small fallback");
  check(fallback.file_size_limit == 12345, "RLIMIT_FSIZE reaches the runtime budget");

  const std::vector<std::string> lines = describe_budget(large);
  check(lines.size() == 1 && lines[0].rfind("budget ", 0) == 0,
        "the budget report has a stable prefix");

  check(ToolExecutor(8).workers() == 8, "the executor exposes the budgeted worker count");

  {
    Concurrency counter;
    std::vector<ToolJob> jobs;
    for (std::size_t i = 0; i < 4; ++i) {
      jobs.push_back(make_job(i, &counter));
    }
    const std::vector<ToolOutcome> outcomes = ToolExecutor(4).run(jobs);
    check(outcomes.size() == 4, "all parallel jobs return an outcome");
    check(counter.peak.load() == 4, "four workers run four jobs concurrently");
  }
  {
    Concurrency counter;
    std::vector<ToolJob> jobs;
    for (std::size_t i = 0; i < 4; ++i) {
      jobs.push_back(make_job(i, &counter));
    }
    const std::vector<ToolOutcome> outcomes = ToolExecutor(1).run(jobs);
    check(outcomes.size() == 4, "serial jobs still all return");
    check(counter.peak.load() == 1, "one worker never runs two jobs at once");
  }

  char directory[] = "/tmp/ps5h-runtime-XXXXXX";
  check(::mkdtemp(directory) != nullptr, "create a temporary directory");
  const StorageBudget storage = download_budget(std::string(directory) + "/payload.bin");
  check(storage.ok, "the destination filesystem is measurable");
  check(storage.free_bytes > 0, "free space is positive");
  check(storage.capacity <= storage.free_bytes, "capacity leaves the reserve in place");
  check(storage.reserve_bytes <= storage.free_bytes, "the reserve is not larger than free space");
  ::rmdir(directory);

  std::printf("runtime: all checks passed\n");
  return 0;
}
