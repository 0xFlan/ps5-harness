// Host tests for the P1 hardening work: path canonicalisation (A1), secret
// redaction (A2), streaming downloads (A3), URL policy (A4) and tool deadlines.

#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "ps5h/core.h"
#include "ps5h/paths.h"
#include "ps5h/policy.h"
#include "ps5h/secrets.h"
#include "ps5h/sha256.h"

using namespace ps5h;

namespace {

// --- helpers -----------------------------------------------------------------

std::string make_temp_dir() {
  char pattern[] = "/tmp/ps5h-test-XXXXXX";
  char* made = ::mkdtemp(pattern);
  assert(made != nullptr);
  return std::string(made);
}

std::string read_text(const std::string& path) {
  std::string out;
  FILE* file = std::fopen(path.c_str(), "rb");
  assert(file != nullptr);
  char buffer[4096];
  std::size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof buffer, file)) > 0) {
    out.append(buffer, read);
  }
  std::fclose(file);
  return out;
}

// Single-request HTTP fixture: no network, no dependency on a second process.
class FixtureServer {
 public:
  explicit FixtureServer(std::string body) : body_(std::move(body)) {
    listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(listener_ >= 0);
    int yes = 1;
    ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    assert(::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0);
    assert(::listen(listener_, 4) == 0);
    socklen_t length = sizeof address;
    assert(::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length) == 0);
    port_ = ntohs(address.sin_port);
    thread_ = std::thread([this]() { serve(); });
  }

  ~FixtureServer() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    ::close(listener_);
  }

  std::string url(const std::string& path) const {
    return "http://127.0.0.1:" + std::to_string(port_) + path;
  }

 private:
  void serve() {
    while (!stop_) {
      // poll() rather than a blocking accept(): closing or shutting down a
      // listening socket does not reliably wake a thread parked in accept().
      pollfd probe{listener_, POLLIN, 0};
      const int ready = ::poll(&probe, 1, 100);
      if (ready <= 0) {
        continue;
      }
      int client = ::accept(listener_, nullptr, nullptr);
      if (client < 0) continue;
      char request[4096];
      const ssize_t read = ::recv(client, request, sizeof request - 1, 0);
      (void)read;
      std::string header = "HTTP/1.1 200 OK\r\nContent-Length: " +
                           std::to_string(body_.size()) +
                           "\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n\r\n";
      ::send(client, header.data(), header.size(), 0);
      std::size_t sent = 0;
      while (sent < body_.size()) {
        const ssize_t chunk = ::send(client, body_.data() + sent, body_.size() - sent, 0);
        if (chunk <= 0) break;
        sent += static_cast<std::size_t>(chunk);
      }
      ::close(client);
      if (++served_ >= 2) return;
    }
  }

  std::string body_;
  int listener_ = -1;
  int port_ = 0;
  int served_ = 0;
  bool stop_ = false;
  std::thread thread_;
};

// --- A1: paths ---------------------------------------------------------------

void test_path_guard() {
  const std::string root = make_temp_dir();
  const std::string inside = root + "/homebrew";
  const std::string sibling = root + "/homebrewX";
  assert(::mkdir(inside.c_str(), 0700) == 0);
  assert(::mkdir(sibling.c_str(), 0700) == 0);

  std::string error;
  PathGuard guard = PathGuard::from_roots({inside}, &error);
  assert(!guard.empty());
  assert(guard.allows(inside));
  assert(guard.allows(inside + "/game.bin"));
  assert(guard.allows(inside + "/new-file.bin"));
  assert(!guard.allows(inside + "/sub/../b"));
  assert(!guard.allows(sibling + "/evil"));
  assert(!guard.allows(inside + "/../escape"));
  assert(!guard.allows("/etc/passwd"));
  assert(!guard.allows("relative/path"));

  // Symlink escape: a link inside the root pointing at a sibling directory.
  const std::string link = inside + "/link";
  assert(::symlink(sibling.c_str(), link.c_str()) == 0);
  assert(!guard.allows(link + "/evil"));

  // Secret paths are recognised after canonicalisation.
  assert(looks_secret(inside + "/config.json"));
  assert(looks_secret(inside + "/creds/token.txt"));
  assert(!looks_secret(inside + "/games/doom.bin"));
}

// --- A2: redaction -----------------------------------------------------------

void test_secret_redaction() {
  SecretSet set;
  set.add("sk-very-secret-value-123456");
  const std::string text = "header sk-very-secret-value-123456 trailer";
  const std::string redacted = set.redact(text);
  assert(redacted.find("sk-very-secret-value-123456") == std::string::npos);
  assert(redacted.find("[redacted]") != std::string::npos);

  // Short values are ignored so redaction cannot shred normal prose.
  SecretSet tiny;
  tiny.add("abc");
  assert(tiny.redact("abc def") == "abc def");
  assert(tiny.empty());
}

// --- A3: sha256 and streaming ------------------------------------------------

void test_sha256_vectors() {
  assert(sha256_hex("") ==
         "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  assert(sha256_hex("abc") ==
         "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

void test_streaming_download() {
  std::string payload;
  payload.reserve(512 * 1024);
  for (int i = 0; i < 512 * 1024; ++i) {
    payload.push_back(static_cast<char>('a' + (i % 26)));
  }
  FixtureServer server(payload);
  const std::string directory = make_temp_dir();
  const std::string target = directory + "/payload.bin";

  DownloadRequest request;
  request.request.url = server.url("/payload.bin");
  request.request.max_body_bytes = 0;
  request.path = target;
  request.expected_sha256 = sha256_hex(payload);

  DownloadResult result = make_download_client()(request, nullptr, nullptr);
  assert(result.ok);
  assert(result.status == 200);
  assert(result.bytes == payload.size());
  assert(read_text(target) == payload);

  // A wrong digest must fail and must not leave the verified name in place.
  DownloadRequest bad = request;
  bad.path = directory + "/bad.bin";
  bad.expected_sha256 = std::string(64, '0');
  DownloadResult bad_result = make_download_client()(bad, nullptr, nullptr);
  assert(!bad_result.ok);
  assert(bad_result.error.find("sha256") != std::string::npos);
  assert(::access(bad.path.c_str(), F_OK) != 0);
}

void test_body_cap() {
  FixtureServer server(std::string(64 * 1024, 'x'));
  HttpRequest request;
  request.url = server.url("/big");
  request.max_body_bytes = 1024;
  HttpResponse response = make_http_client()(request);
  assert(!response.error.empty());
  assert(response.error.find("exceeds") != std::string::npos);
  assert(response.body.size() <= 1024);
}

// --- A4: URL policy ----------------------------------------------------------

void test_url_policy() {
  UrlPolicy policy;
  assert(policy.check("file:///etc/passwd").reason.find("scheme") != std::string::npos);
  assert(!policy.check("file:///etc/passwd").allowed);
  assert(!policy.check("http://example.com/x").allowed);
  assert(!policy.check("https://127.0.0.1/x").allowed);
  assert(!policy.check("https://10.0.0.5/x").allowed);
  assert(!policy.check("https://192.168.0.10:8443/x").allowed);
  assert(!policy.check("https://169.254.1.1/x").allowed);
  assert(!policy.check("https://[::1]/x").allowed);
  assert(!policy.check("not a url").allowed);

  // A public literal needs no DNS, so this stays offline-deterministic.
  auto public_ip = policy.check("https://1.1.1.1/x");
  assert(public_ip.allowed);
  assert(!public_ip.resolved_ip.empty());

  UrlPolicy restricted;
  restricted.allowed_hosts.push_back("1.1.1.1");
  assert(restricted.check("https://1.1.1.1/x").allowed);
  assert(!restricted.check("https://8.8.8.8/x").allowed);
}

// --- A6: deadlines -----------------------------------------------------------

void test_tool_deadline() {
  Tool slow;
  slow.name = "slow";
  slow.timeout_ms = 50;
  slow.handler = [](const Json&) -> Json {
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    return Json("late");
  };
  std::string error;
  Json result = run_with_deadline(slow.handler, Json::object(), slow.timeout_ms, &error);
  assert(!error.empty());
  assert(error.find("timed out") != std::string::npos);
  assert(result.is_null());

  Tool fast;
  fast.handler = [](const Json&) -> Json { return Json("ok"); };
  std::string fast_error;
  Json fast_result = run_with_deadline(fast.handler, Json::object(), 2000, &fast_error);
  assert(fast_error.empty());
  assert(fast_result.as_string() == "ok");
}

}  // namespace

int main() {
  std::signal(SIGPIPE, SIG_IGN);
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  std::printf("paths...\n");
  test_path_guard();
  std::printf("redaction...\n");
  test_secret_redaction();
  std::printf("sha256...\n");
  test_sha256_vectors();
  std::printf("streaming...\n");
  test_streaming_download();
  std::printf("body cap...\n");
  test_body_cap();
  std::printf("policy...\n");
  test_url_policy();
  std::printf("deadline...\n");
  test_tool_deadline();
  std::printf("hardening tests passed\n");
  return 0;
}
