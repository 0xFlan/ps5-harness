// P2 bring-up payload. Its only job is to prove, on the console, that the
// cross-built artifact runs far enough to report which build it is and how much
// space each candidate mount has. No tools, no network, no plugins.
//
// Host-verified: compiles to an x86-64 FreeBSD ELF and packs to a fake SELF.
// Hardware-unverified: nothing here has run on a PS5.

#include <cstdio>
#include <cstring>
#include <sys/statvfs.h>

#ifndef PS5H_BUILD_ID
#define PS5H_BUILD_ID "unknown"
#endif

namespace {

struct Mount {
  const char* path;
  const char* log_path;
};

// /data is the console's writable data mount; the USB paths are where a
// jailbroken console usually exposes sticks. Report each one we can stat, so a
// single run answers "which of these exist and how big are they".
constexpr Mount kMounts[] = {
    {"/data", "/data/ps5-harness-bringup.log"},
    {"/user/homebrew", "/user/homebrew/ps5-harness-bringup.log"},
    {"/mnt/usb0", "/mnt/usb0/ps5-harness-bringup.log"},
    {"/mnt/usb1", "/mnt/usb1/ps5-harness-bringup.log"},
};

void report(std::FILE* out, const char* text) {
  std::fputs(text, out);
  std::fputc('\n', out);
}

}  // namespace

int main() {
  char line[256];
  std::snprintf(line, sizeof line, "ps5-harness bringup build=%s", PS5H_BUILD_ID);

  // stdout only reaches the loader socket, so also append to the first mount
  // that accepts a write; a title launched from the shell has no terminal.
  std::FILE* log = nullptr;
  for (const Mount& mount : kMounts) {
    struct statvfs vfs {};
    if (statvfs(mount.path, &vfs) != 0) {
      continue;
    }
    const unsigned long long free_mib =
        static_cast<unsigned long long>(vfs.f_bavail) * vfs.f_frsize / (1024ULL * 1024ULL);
    std::snprintf(line, sizeof line, "%s free %s = %llu MiB (block %lu)",
                  PS5H_BUILD_ID, mount.path, free_mib,
                  static_cast<unsigned long>(vfs.f_frsize));
    std::printf("%s\n", line);
    if (log == nullptr) {
      log = std::fopen(mount.log_path, "a");
      if (log != nullptr) {
        std::snprintf(line, sizeof line, "ps5-harness bringup build=%s", PS5H_BUILD_ID);
        report(log, line);
      }
    }
    if (log != nullptr) {
      report(log, line);
    }
  }
  if (log != nullptr) {
    std::fclose(log);
  }
  return 0;
}
