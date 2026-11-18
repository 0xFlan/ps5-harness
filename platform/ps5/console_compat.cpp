// Console-only glue for the real agent. This file is compiled only into the
// PS5 target; it is never part of the host build.
//
// Two behaviours here are hardware-observed on PS5 titles (see the PacBrew
// libcurl notes recorded in BUILD.md):
//   - a sandboxed title's libc rejects fcntl(F_SETFD, FD_CLOEXEC) on sockets
//     with EINVAL, which makes every curl connect fail;
//   - curl's fcntl(O_NONBLOCK) does not put console sockets in non-blocking
//     mode, so requests wait for the server to close the connection.
// The wrapper keeps files and pipes on the real path and maps only the socket
// cases to the console's own SO_NBIO option.
//
// The CA bundle is the console's own CA_LIST.cer. Its path differs between a
// sandboxed and an elevated title, so both are probed on each call.

#include <fcntl.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// libkernel: the random word that names this title's sandbox root. Declared
// here because the payload SDK headers do not ship the native kernel headers.
extern "C" const char* sceKernelGetFsSandboxRandomWord(void);

extern "C" int __real_fcntl(int fd, int cmd, ...);

namespace {

constexpr int kSoNbio = 0x1200;

bool readable(const char* path) { return path != nullptr && ::access(path, R_OK) == 0; }

}  // namespace

extern "C" int ps5h_console_nonblocking(int fd) {
  const int on = 1;
  return ::setsockopt(fd, SOL_SOCKET, kSoNbio, &on, sizeof on);
}

extern "C" const char* ps5h_console_ca_bundle() {
  // Re-probe on every call: a title that elevates after starting moves the CA
  // list. Thread-local storage keeps parallel tool calls from sharing a path.
  thread_local char path[512];
  path[0] = '\0';

  const char* override_path = std::getenv("PS5H_CA_BUNDLE");
  if (readable(override_path)) {
    std::snprintf(path, sizeof path, "%s", override_path);
    return path;
  }

  const char* word = sceKernelGetFsSandboxRandomWord();
  if (word != nullptr && word[0] != '\0') {
    std::snprintf(path, sizeof path, "/%s/common/cert/CA_LIST.cer", word);
    if (readable(path)) {
      return path;
    }
  }

  const char* system_path = "/system/common/cert/CA_LIST.cer";
  if (readable(system_path)) {
    std::snprintf(path, sizeof path, "%s", system_path);
    return path;
  }

  path[0] = '\0';
  return nullptr;
}

extern "C" int __wrap_fcntl(int fd, int cmd, ...) {
  va_list arguments;
  va_start(arguments, cmd);
  const long argument = va_arg(arguments, long);
  va_end(arguments);

  const int result = __real_fcntl(fd, cmd, argument);
  if (result != -1 || errno != EINVAL) {
    return result;
  }

  if (cmd == F_SETFD && (argument & FD_CLOEXEC) != 0) {
    // A title never execs another program; accepting CLOEXEC is safe and is
    // what curl needs before it will connect.
    errno = 0;
    return 0;
  }

  if (cmd == F_SETFL && (argument & O_NONBLOCK) != 0) {
    if (ps5h_console_nonblocking(fd) == 0) {
      return 0;
    }
    // A regular file is already "non-blocking enough"; only sockets need the
    // console option. Do not turn a harmless file flag into a hard failure.
    if (errno == ENOTSOCK) {
      errno = 0;
      return 0;
    }
  }
  return result;
}
