#include "ps5h/local_model.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// From llama.cpp's tools/server. The implementation is linked into the harness;
// this is not a second application and does not require PS5LM Plus.
int llama_server(int argc, char** argv);
void llama_server_terminate();

namespace ps5h {
namespace {

// The HTTP listener opens before the GGUF finishes loading, so a TCP connect
// is not enough. /health changes from 503 to 200 only after the model is ready.
bool health_ready(int port) {
  static int attempts = 0;
  const bool trace = attempts < 3 || (attempts % 40) == 0;
  ++attempts;
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return false;
  // A listener that accepts but has not finished starting must not stall the
  // readiness loop indefinitely; retry on the next tick instead.
  timeval timeout{};
  timeout.tv_sec = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<unsigned short>(port));
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  const int connected =
      ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof address);
  if (connected != 0) {
    if (trace) {
      std::fprintf(stderr, "[local] health attempt %d connect failed errno=%d\n",
                   attempts, errno);
      std::fflush(stderr);
    }
    ::close(fd);
    return false;
  }
  const char request[] =
      "GET /health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
  const ssize_t sent = ::send(fd, request, sizeof request - 1, 0);
  if (sent < 0) {
    if (trace) {
      std::fprintf(stderr, "[local] health attempt %d send failed errno=%d\n",
                   attempts, errno);
      std::fflush(stderr);
    }
    ::close(fd);
    return false;
  }
  char response[128] = {0};
  const ssize_t got = ::recv(fd, response, sizeof response - 1, 0);
  if (trace) {
    std::fprintf(stderr, "[local] health attempt %d sent=%zd got=%zd response=%s\n",
                 attempts, sent, got, response);
    std::fflush(stderr);
  }
  ::close(fd);
  if (got <= 0) return false;
  return std::strstr(response, " 200 ") != nullptr;
}

void local_note(const char* text) {
  std::fprintf(stderr, "[local] %s\n", text);
  std::fflush(stderr);
}

}  // namespace

LocalModelRuntime::~LocalModelRuntime() { stop(); }

bool LocalModelRuntime::start(const std::string& model_path, int port,
                              int threads, int context, std::string* error) {
  if (error != nullptr) error->clear();
  if (ready_.load()) return true;
  if (model_path.empty() || model_path.size() > 4096) {
    if (error != nullptr) *error = "local model path is empty or too long";
    return false;
  }
  local_note(("opening " + model_path).c_str());
  struct stat info {};
  if (::stat(model_path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
    if (error != nullptr) *error = "local model file not found: " + model_path;
    return false;
  }
  local_note(("model bytes " + std::to_string(static_cast<long long>(info.st_size))).c_str());
  if (port < 1 || port > 65535) {
    if (error != nullptr) *error = "local model port is invalid";
    return false;
  }
  if (threads < 1) threads = 1;
  if (threads > 8) threads = 8;
  if (context < 512) context = 512;

  std::vector<std::string> args = {
      "ps5h-local-model",
      "--model",
      model_path,
      "--alias",
      "local",
      "--host",
      "127.0.0.1",
      "--port",
      std::to_string(port),
      "--ctx-size",
      std::to_string(context),
      "--threads",
      std::to_string(threads),
      "--jinja",
      "--reasoning",
      "off",
      "--no-webui",
      "--parallel",
      "1",
  };
  // The console default pthread stack is too small for llama.cpp's server
  // startup. PS5LM uses 16 MiB for the same work; use an explicit pthread so
  // this runtime gets that stack instead of std::thread's default.
  auto* launch = new std::pair<LocalModelRuntime*, std::vector<std::string>>(
      this, std::move(args));
  pthread_attr_t attributes;
  if (pthread_attr_init(&attributes) != 0 ||
      pthread_attr_setstacksize(&attributes, 16u << 20) != 0) {
    delete launch;
    if (error != nullptr) *error = "could not create the local model thread";
    return false;
  }
  const int created =
      pthread_create(&thread_, &attributes, &LocalModelRuntime::run, launch);
  pthread_attr_destroy(&attributes);
  if (created != 0) {
    delete launch;
    if (error != nullptr) *error = "local model thread creation failed";
    return false;
  }
  thread_started_ = true;
  local_note("llama.cpp server thread started");

  // llama-server opens its listener only after the GGUF is loaded, so a
  // successful loopback connect is a real readiness signal, not just an
  // allocated thread.
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(180);
  while (std::chrono::steady_clock::now() < deadline) {
    if (health_ready(port)) {
      ready_ = true;
      return true;
    }
    if (finished_.load()) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }

  std::string message;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (failure_.empty()) {
      failure_ = "local model server did not become ready";
    }
    message = failure_;
  }
  if (error != nullptr) *error = message;
  stop();
  return false;
}

void* LocalModelRuntime::run(void* context) {
  auto* launch =
      static_cast<std::pair<LocalModelRuntime*, std::vector<std::string>>*>(
          context);
  LocalModelRuntime* runtime = launch->first;
  std::vector<std::string> args = std::move(launch->second);
  delete launch;

  std::vector<char*> argv;
  argv.reserve(args.size());
  for (const std::string& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  try {
    const int result = llama_server(static_cast<int>(argv.size()), argv.data());
    if (result != 0) {
      std::lock_guard<std::mutex> lock(runtime->mutex_);
      runtime->failure_ =
          "llama.cpp server exited with status " + std::to_string(result);
    }
  } catch (const std::exception& failure) {
    std::lock_guard<std::mutex> lock(runtime->mutex_);
    runtime->failure_ = std::string("llama.cpp server failed: ") + failure.what();
  } catch (...) {
    std::lock_guard<std::mutex> lock(runtime->mutex_);
    runtime->failure_ = "llama.cpp server failed";
  }
  runtime->finished_ = true;
  return nullptr;
}

void LocalModelRuntime::stop() {
  if (thread_started_) {
    // shutdown_handler captures stack objects inside llama_server. Calling it
    // after that function has returned dereferences dead state, so only ask a
    // still-running server to stop.
    if (!finished_.load()) {
      llama_server_terminate();
    }
    pthread_join(thread_, nullptr);
    thread_started_ = false;
  }
  ready_ = false;
}

}  // namespace ps5h
