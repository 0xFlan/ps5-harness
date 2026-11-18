// Host-side assertions for the resource probe. The numbers are this machine's;
// what the test proves is that the probe returns sane values, enumerates
// mounts, and reports a bad candidate instead of throwing or aborting.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "ps5h/resources.h"

namespace {

void check(bool condition, const char* what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s\n", what);
    std::exit(1);
  }
  std::printf("ok %s\n", what);
}

}  // namespace

int main() {
  using namespace ps5h;

  const ResourceProfile profile = probe_resources({"/"});
  check(profile.cpu_online >= 1, "cpu online is at least one");
  check(profile.cpu_configured >= profile.cpu_online,
        "configured cpus are not below online cpus");
  check(profile.page_bytes > 0, "page size is reported");
  check(profile.memory_total_bytes > 0, "physical memory is reported");
  check(!profile.mounts.empty(), "system mounts are enumerated");
  check(profile.candidates.size() == 1 && profile.candidates[0].ok,
        "the root candidate is probed");
  check(profile.candidates[0].total_bytes > 0, "the root has a non-zero total");

  const ResourceProfile missing =
      probe_resources({"/ps5h-resource-probe-does-not-exist"});
  check(missing.candidates.size() == 1 && !missing.candidates[0].ok,
        "a missing candidate reports failure");
  check(missing.candidates[0].error != 0, "a missing candidate records errno");

  const std::vector<std::string> lines = describe_resources(profile);
  check(lines.size() >= 4, "report has cpu, memory, limits and mount lines");
  check(lines[0].rfind("cpu ", 0) == 0, "first line is cpu");
  check(lines[1].rfind("mem ", 0) == 0, "second line is memory");
  check(lines[1].find("available=") != std::string::npos,
        "the memory line names available memory");
  check(lines[2].rfind("limit ", 0) == 0, "third line is limits");
  check(lines[3].rfind("mount ", 0) == 0, "mount lines follow");

  check(format_bytes(0) == "0 MiB", "zero bytes formats");
  check(format_bytes(1024ULL * 1024ULL * 1536ULL) == "1536 MiB",
        "1536 MiB formats without rounding");

  std::printf("resources: all checks passed\n");
  return 0;
}
