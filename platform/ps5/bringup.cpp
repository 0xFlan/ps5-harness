// P2 bring-up payload. Its only job is to prove, on the console, that the
// cross-built artifact runs far enough to report which build it is, where it is
// running, and how much space each candidate mount has. No tools, no network,
// no plugins.
//
// Host-verified: compiles to an x86-64 FreeBSD ELF, packs to a fake SELF, and
// the same source built for the host writes its log. Hardware-unverified:
// nothing here has run on a PS5.
//
// Logging is deliberately fd-based and the fatal-signal handler is
// async-signal-safe: on a console there is no debugger, so a crash has to leave
// a line behind rather than a system error dialog.

#include <pthread.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

#include "ps5h/resources.h"

#ifndef PS5H_BUILD_ID
#define PS5H_BUILD_ID "unknown"
#endif

#ifndef PS5H_TITLE_ID
#define PS5H_TITLE_ID "unknown"
#endif

namespace {

int g_log_fd = -1;

// Bounded so a machine with many CPUs cannot turn the probe into a fork bomb.
constexpr int kMaxProbeThreads = 16;

// Every mount worth reporting. The log file itself goes to the first path in
// kLogPaths that accepts a create/append.
constexpr const char* kMounts[] = {
    "/data",
    "/user/homebrew",
    "/mnt/usb0",
    "/mnt/usb1",
};

// /data is the documented writable mount; the app's own folder is the fallback
// when the sandbox denies /data (a fake-SELF payload may have no cwd write
// either, which the log line then makes obvious).
constexpr const char* kLogPaths[] = {
    "/data/ps5-harness-bringup.log",
    "/user/homebrew/ps5-harness-bringup.log",
    "./ps5-harness-bringup.log",
    "/mnt/usb0/ps5-harness-bringup.log",
    "/mnt/usb1/ps5-harness-bringup.log",
};

void* thread_probe(void*) { return nullptr; }

void write_all(int fd, const char* text, std::size_t length) {
  while (length > 0) {
    const ssize_t written = ::write(fd, text, length);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return;
    }
    text += written;
    length -= static_cast<std::size_t>(written);
  }
}

void log_line(const char* text) {
  char buffer[1024];
  const int length = std::snprintf(buffer, sizeof buffer, "%s\n", text);
  if (length <= 0) {
    return;
  }
  const std::size_t size =
      static_cast<std::size_t>(length) < sizeof buffer ? static_cast<std::size_t>(length)
                                                       : sizeof buffer - 1;
  write_all(STDOUT_FILENO, buffer, size);
  if (g_log_fd >= 0) {
    write_all(g_log_fd, buffer, size);
  }
}

// --- async-signal-safe crash reporting -------------------------------------

char* append_text(char* out, char* end, const char* text) {
  while (*text != '\0' && out < end) {
    *out++ = *text++;
  }
  return out;
}

char* append_unsigned(char* out, char* end, unsigned long long value, unsigned base) {
  char digits[24];
  int count = 0;
  do {
    const unsigned digit = static_cast<unsigned>(value % base);
    digits[count++] = static_cast<char>(digit < 10 ? '0' + digit : 'a' + (digit - 10));
    value /= base;
  } while (value != 0 && count < static_cast<int>(sizeof digits));
  while (count > 0 && out < end) {
    *out++ = digits[--count];
  }
  return out;
}

const char* signal_name(int signal) {
  switch (signal) {
    case SIGABRT: return "SIGABRT";
    case SIGBUS: return "SIGBUS";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    case SIGSEGV: return "SIGSEGV";
#ifdef SIGSYS
    case SIGSYS: return "SIGSYS";
#endif
    case SIGTRAP: return "SIGTRAP";
    default: return "signal";
  }
}

void on_fatal_signal(int signal, siginfo_t* info, void* /*context*/) {
  char buffer[256];
  char* out = buffer;
  char* const end = buffer + sizeof buffer;
  out = append_text(out, end, "ps5-harness bringup CRASH build=");
  out = append_text(out, end, PS5H_BUILD_ID);
  out = append_text(out, end, " title=");
  out = append_text(out, end, PS5H_TITLE_ID);
  out = append_text(out, end, " signal=");
  out = append_unsigned(out, end, static_cast<unsigned long long>(signal), 10);
  out = append_text(out, end, " (");
  out = append_text(out, end, signal_name(signal));
  out = append_text(out, end, ") addr=");
  const unsigned long long address =
      info != nullptr ? reinterpret_cast<unsigned long long>(info->si_addr) : 0ULL;
  out = append_unsigned(out, end, address, 16);
  out = append_text(out, end, "\n");
  const std::size_t length = static_cast<std::size_t>(out - buffer);
  if (g_log_fd >= 0) {
    write_all(g_log_fd, buffer, length);
  }
  write_all(STDERR_FILENO, buffer, length);
  _exit(128 + signal);
}

void install_crash_handlers() {
  static char alternate_stack[32 * 1024];
  stack_t stack {};
  stack.ss_sp = alternate_stack;
  stack.ss_size = sizeof alternate_stack;
  if (sigaltstack(&stack, nullptr) != 0) {
    log_line("ps5-harness bringup: sigaltstack failed; crash handler may not "
             "survive a stack overflow");
  }

  struct sigaction action {};
  action.sa_sigaction = on_fatal_signal;
  action.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&action.sa_mask);
  const int signals[] = {SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV, SIGTRAP};
  for (const int signal : signals) {
    if (sigaction(signal, &action, nullptr) != 0) {
      char line[128];
      std::snprintf(line, sizeof line,
                    "ps5-harness bringup: sigaction(%d) failed errno=%d", signal, errno);
      log_line(line);
    }
  }
}

bool open_log() {
  for (const char* path : kLogPaths) {
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
      g_log_fd = fd;
      char line[256];
      std::snprintf(line, sizeof line, "ps5-harness bringup log=%s", path);
      log_line(line);
      return true;
    }
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  char line[256];
  std::snprintf(line, sizeof line,
                "ps5-harness bringup build=%s title=%s pid=%ld uid=%ld", PS5H_BUILD_ID,
                PS5H_TITLE_ID, static_cast<long>(::getpid()), static_cast<long>(::getuid()));
  std::printf("%s\n", line);

  if (!open_log()) {
    // stdout is the loader socket; say so loudly because there will be no file.
    std::printf("ps5-harness bringup: no writable log path tried\n");
  }

  log_line(line);

  char cwd[256];
  if (::getcwd(cwd, sizeof cwd) != nullptr) {
    std::snprintf(line, sizeof line, "ps5-harness bringup cwd=%s argc=%d", cwd, argc);
  } else {
    std::snprintf(line, sizeof line, "ps5-harness bringup cwd=<denied> argc=%d", argc);
  }
  log_line(line);
  if (argc > 0 && argv != nullptr && argv[0] != nullptr) {
    std::snprintf(line, sizeof line, "ps5-harness bringup argv0=%s", argv[0]);
    log_line(line);
  }

  install_crash_handlers();

  // Everything an agent would inherit: the CPUs this process may run on, the
  // memory it can see, the loader's limits, every mounted filesystem, and the
  // four writable candidates the harness cares about.
  std::vector<std::string> candidates;
  for (const char* mount : kMounts) {
    candidates.emplace_back(mount);
  }
  const ps5h::ResourceProfile resources = ps5h::probe_resources(candidates);
  for (const std::string& description : ps5h::describe_resources(resources)) {
    log_line(("ps5-harness bringup " + description).c_str());
  }

  // A thread per online CPU, bounded. Creating them proves the payload can use
  // more than the thread it started on; joining them leaves nothing running.
  int requested_threads = resources.cpu_online > 0 ? resources.cpu_online : 1;
  if (requested_threads > kMaxProbeThreads) {
    requested_threads = kMaxProbeThreads;
  }
  pthread_t workers[kMaxProbeThreads];
  int created_threads = 0;
  for (int i = 0; i < requested_threads; ++i) {
    if (pthread_create(&workers[created_threads], nullptr, thread_probe, nullptr) != 0) {
      break;
    }
    ++created_threads;
  }
  for (int i = 0; i < created_threads; ++i) {
    pthread_join(workers[i], nullptr);
  }
  std::snprintf(line, sizeof line,
                "ps5-harness bringup threads created=%d requested=%d",
                created_threads, requested_threads);
  log_line(line);

  // One bounded heap probe: a process that can only allocate its own image is
  // not a place an agent can live. Touching every page defeats overcommit.
  constexpr std::size_t kAllocProbeBytes = 32u * 1024u * 1024u;
  void* allocation = std::malloc(kAllocProbeBytes);
  bool allocation_ok = false;
  if (allocation != nullptr) {
    volatile unsigned char* bytes = static_cast<volatile unsigned char*>(allocation);
    for (std::size_t offset = 0; offset < kAllocProbeBytes; offset += 4096) {
      bytes[offset] = 1;
    }
    allocation_ok = bytes[kAllocProbeBytes - 1] == 1;
    std::free(allocation);
  }
  std::snprintf(line, sizeof line, "ps5-harness bringup alloc request=%zu ok=%d",
                kAllocProbeBytes, allocation_ok ? 1 : 0);
  log_line(line);

  log_line("ps5-harness bringup: done");
  if (g_log_fd >= 0) {
    ::fsync(g_log_fd);
    ::close(g_log_fd);
    g_log_fd = -1;
  }
  return 0;
}
