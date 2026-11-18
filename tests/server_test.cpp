#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "ps5h/agent.h"
#include "ps5h/core.h"
#include "ps5h/json.h"
#include "ps5h/policy.h"
#include "ps5h/secrets.h"
#include "ps5h/server.h"
#include "ps5h/session.h"

using namespace ps5h;

namespace {

int free_port() {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  assert(::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0);
  socklen_t length = sizeof address;
  assert(::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) == 0);
  const int port = ntohs(address.sin_port);
  ::close(fd);
  return port;
}

std::string request(int port, const std::string& method, const std::string& path,
                    const std::string& token,
                    const std::string& body = std::string()) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(static_cast<unsigned short>(port));
  assert(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0);
  std::string head = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n";
  if (!token.empty()) head += "Authorization: Bearer " + token + "\r\n";
  if (!body.empty()) {
    head += "Content-Type: application/json\r\nContent-Length: " +
            std::to_string(body.size()) + "\r\n";
  }
  head += "Connection: close\r\n\r\n";
  assert(::send(fd, head.data(), head.size(), 0) ==
         static_cast<ssize_t>(head.size()));
  if (!body.empty()) {
    assert(::send(fd, body.data(), body.size(), 0) ==
           static_cast<ssize_t>(body.size()));
  }
  std::string response;
  char buffer[4096];
  ssize_t got = 0;
  while ((got = ::recv(fd, buffer, sizeof buffer, 0)) > 0) {
    response.append(buffer, static_cast<std::size_t>(got));
  }
  ::close(fd);
  return response;
}

std::string body_of(const std::string& response) {
  const std::size_t split = response.find("\r\n\r\n");
  assert(split != std::string::npos);
  return response.substr(split + 4);
}

int wait_for_server(int port) {
  for (int i = 0; i < 100; ++i) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<unsigned short>(port));
    const int result =
        ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof address);
    ::close(fd);
    if (result == 0) return 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return -1;
}

int child_server(int port, const std::string& token) {
  std::freopen("/dev/null", "w", stderr);
  ToolRegistry registry;
  ModelConfig config;
  config.base_url = "http://127.0.0.1:9/v1";
  config.model = "stub";
  UrlPolicy policy;
  policy.https_only = false;
  policy.deny_private_ranges = false;
  HttpFn http = [](const HttpRequest&) -> HttpResponse {
    HttpResponse response;
    response.status = 200;
    response.body =
        "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"hello from stub\"}}],\"usage\":{\"prompt_tokens\":3,"
        "\"completion_tokens\":2,\"total_tokens\":5}}";
    return response;
  };
  LogFn quiet = [](const std::string&) {};
  RuntimeBudget budget;
  budget.workers = 1;
  Agent agent(config, http, registry,
              [](const std::string&) { return std::string(); }, quiet, quiet,
              policy, SecretSet{}, budget);
  AgentOptions options;
  options.max_steps = 2;
  options.system_prompt = "server test";
  ChatSession session;
  session.reset(options.system_prompt);
  session.context_chars = 8000;
  ActivityLog activity;
  ServerOptions server;
  server.port = port;
  server.token = token;
  server.build_id = "test";
  server.model = "stub";
  server.context_chars = 8000;
  return run_server(server, agent, session, options, registry, activity, quiet);
}

}  // namespace

int main() {
  const int port = free_port();
  const std::string token = "0123456789abcdef";
  const pid_t child = ::fork();
  assert(child >= 0);
  if (child == 0) {
    _exit(child_server(port, token));
  }
  assert(wait_for_server(port) == 0);

  const std::string page = request(port, "GET", "/", "");
  assert(page.find("200 OK") != std::string::npos);
  assert(body_of(page).find("PS5 Harness") != std::string::npos);

  const std::string unauth = request(port, "GET", "/api/state", "");
  assert(unauth.find("401 Unauthorized") != std::string::npos);

  const std::string state = request(port, "GET", "/api/state", token);
  assert(state.find("200 OK") != std::string::npos);
  const Json state_json = Json::parse(body_of(state));
  assert(state_json["mode"].as_string() == "full");
  assert(state_json["tools"].is_array());
  assert(state_json["context"]["limit"].as_number() == 8000);

  const std::string chat = request(
      port, "POST", "/api/chat", token,
      "{\"message\":\"server round trip\"}");
  assert(chat.find("200 OK") != std::string::npos);
  const Json chat_json = Json::parse(body_of(chat));
  assert(chat_json["ok"].as_bool(false));
  assert(chat_json["answer"].as_string() == "hello from stub");
  assert(chat_json["usage"]["total_tokens"].as_number() == 5);

  const std::string history = request(port, "GET", "/api/history", token);
  assert(history.find("200 OK") != std::string::npos);
  const Json history_json = Json::parse(body_of(history));
  assert(history_json["messages"].size() == 3);  // system, user, assistant

  ::kill(child, SIGTERM);
  int status = 0;
  ::waitpid(child, &status, 0);
  std::printf("server_test: ok (port %d)\n", port);
  return 0;
}
