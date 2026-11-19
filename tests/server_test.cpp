#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
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
#include "ps5h/console_ui.h"
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

int free_udp_port() {
  const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
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

std::string udp_discover(int port) {
  const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  assert(fd >= 0);
  timeval timeout{};
  timeout.tv_sec = 2;
  assert(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(static_cast<unsigned short>(port));
  const std::string query = "PS5H_DISCOVER_V1\n";
  assert(::sendto(fd, query.data(), query.size(), 0,
                  reinterpret_cast<sockaddr*>(&address), sizeof address) ==
         static_cast<ssize_t>(query.size()));
  char buffer[4096];
  const ssize_t got = ::recv(fd, buffer, sizeof buffer, 0);
  ::close(fd);
  assert(got > 0);
  return std::string(buffer, static_cast<std::size_t>(got));
}

std::string request(int port, const std::string& method, const std::string& path,
                    const std::string& token,
                    const std::string& body = std::string(),
                    bool tolerate_failure = false) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);
  timeval timeout{};
  timeout.tv_sec = 2;
  assert(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(static_cast<unsigned short>(port));
  if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) {
    ::close(fd);
    if (!tolerate_failure) assert(false);
    return std::string();
  }
  std::string head = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n";
  if (!token.empty()) head += "Authorization: Bearer " + token + "\r\n";
  if (!body.empty()) {
    head += "Content-Type: application/json\r\nContent-Length: " +
            std::to_string(body.size()) + "\r\n";
  }
  head += "Connection: close\r\n\r\n";
  const ssize_t head_sent = ::send(fd, head.data(), head.size(), 0);
  if (head_sent != static_cast<ssize_t>(head.size())) {
    ::close(fd);
    if (!tolerate_failure) assert(false);
    return std::string();
  }
  if (!body.empty()) {
    const ssize_t body_sent = ::send(fd, body.data(), body.size(), 0);
    if (body_sent != static_cast<ssize_t>(body.size())) {
      ::close(fd);
      if (!tolerate_failure) assert(false);
      return std::string();
    }
  }
  std::string response;
  char buffer[4096];
  ssize_t got = 0;
  while ((got = ::recv(fd, buffer, sizeof buffer, 0)) > 0) {
    response.append(buffer, static_cast<std::size_t>(got));
  }
  ::close(fd);
  if (got < 0 && !tolerate_failure) assert(false);
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

int child_server(int port, int discovery_port, const std::string& token,
                 const std::string& network_gate) {
  std::freopen("/dev/null", "w", stderr);
  assert(::setenv("PS5H_TEST_NETWORK_GATE", network_gate.c_str(), 1) == 0);
  ToolRegistry registry;
  ModelConfig config;
  config.base_url = "http://127.0.0.1:9/v1";
  config.model = "stub";
  UrlPolicy policy;
  policy.https_only = false;
  policy.deny_private_ranges = false;
  HttpFn http = [](const HttpRequest& request) -> HttpResponse {
    std::string model = "stub";
    try {
      const Json body = Json::parse(request.body);
      model = body["model"].as_string("stub");
    } catch (...) {
    }
    HttpResponse response;
    response.status = 200;
    response.body =
        "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":"
        "\"hello from " + model +
        "\"}}],\"usage\":{\"prompt_tokens\":3,"
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
  server.discovery_port = discovery_port;
  server.token = token;
  server.build_id = "test";
  server.model = "stub";
  server.policy = policy;
  server.context_chars = 8000;
  // One console surface, as the harness creates for --serve.
  server.ui = std::make_shared<ConsoleUi>();
  return run_server(server, agent, session, options, registry, activity, quiet);
}

void set_network_gate(const std::string& path, bool down) {
  if (down) {
    const int fd = ::open(path.c_str(), O_CREAT | O_WRONLY, 0600);
    assert(fd >= 0);
    assert(::write(fd, "x", 1) == 1);
    ::close(fd);
    return;
  }
  assert(::unlink(path.c_str()) == 0);
}

long long wait_for_generation(int port, const std::string& token,
                              long long previous, int timeout_ms = 10000) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    const std::string response =
        request(port, "GET", "/api/health", token, std::string(), true);
    if (response.find("200 OK") != std::string::npos) {
      try {
        const Json health = Json::parse(body_of(response));
        const long long generation = health["listener_generation"].as_number();
        if (generation > previous) return generation;
      } catch (...) {
        // A partial response is expected across the close/rebind boundary.
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  assert(false);
  return previous;
}

}  // namespace

int main() {
  const int port = free_port();
  const int discovery_port = free_udp_port();
  const std::string token = "0123456789abcdef";
  const std::string network_gate =
      "/tmp/ps5h-server-test-network-gate-" + std::to_string(::getpid());
  ::unlink(network_gate.c_str());
  const pid_t child = ::fork();
  assert(child >= 0);
  if (child == 0) {
    _exit(child_server(port, discovery_port, token, network_gate));
  }
  assert(wait_for_server(port) == 0);

  const std::string page = request(port, "GET", "/", "");
  assert(page.find("200 OK") != std::string::npos);
  const std::string page_body = body_of(page);
  assert(page_body.find("PS5 Harness") != std::string::npos);
  // The workspace carries a keyboard/controller/touchpad/dictation input layer.
  assert(page_body.find("id=\"osk\"") != std::string::npos);
  assert(page_body.find("id=\"mic-button\"") != std::string::npos);
  assert(page_body.find("id=\"input-pill\"") != std::string::npos);
  assert(page_body.find("id=\"virtual-pointer\"") != std::string::npos);
  assert(page_body.find("navigator.getGamepads") != std::string::npos);
  assert(page_body.find("pad.touches") != std::string::npos);
  assert(page_body.find("SpeechRecognition") != std::string::npos);
  assert(page_body.find("id=\"panel-home\"") != std::string::npos);
  assert(page_body.find("id=\"panel-model\"") != std::string::npos);
  assert(page_body.find("Model &amp; provider") != std::string::npos);
  assert(page_body.find("/api/model") != std::string::npos);
  assert(page_body.find("Home screen app") != std::string::npos);
  assert(page_body.find("/tile-icon.png") != std::string::npos);
  assert(page_body.find("id=\"reconnect\"") != std::string::npos);
  assert(page_body.find("Console connection lost") != std::string::npos);
  assert(page_body.find("id=\"reconnect-url\"") != std::string::npos);
  assert(page_body.find("If the console went to rest mode") != std::string::npos);
  assert(page_body.find("If the console was restarted") == std::string::npos);

  const std::string icon = request(port, "GET", "/tile-icon.png", "");
  assert(icon.find("200 OK") != std::string::npos);
  assert(icon.find("Content-Type: image/png") != std::string::npos);
  const std::string icon_body = body_of(icon);
  assert(icon_body.size() > 1000);
  assert(static_cast<unsigned char>(icon_body[0]) == 0x89);
  assert(icon_body.compare(1, 3, "PNG") == 0);

  const std::string unauth = request(port, "GET", "/api/state", "");
  assert(unauth.find("401 Unauthorized") != std::string::npos);

  const std::string discovery = request(port, "GET", "/.well-known/ps5h", "");
  assert(discovery.find("200 OK") != std::string::npos);
  const std::string discovery_body = body_of(discovery);
  assert(discovery_body.find(token) == std::string::npos);
  const Json discovery_json = Json::parse(discovery_body);
  assert(discovery_json["service"].as_string() == "ps5-harness");
  assert(discovery_json["protocol"].as_number() == 1);
  assert(discovery_json["http_port"].as_number() == port);
  assert(discovery_json["title_id"].as_string() == "HARN00001");
  assert(discovery_json["addresses"].is_array());
  assert(discovery_json["urls"].is_array());

  const Json udp_json = Json::parse(udp_discover(discovery_port));
  assert(udp_json["service"].as_string() == "ps5-harness");
  assert(udp_json["http_port"].as_number() == port);
  assert(udp_json["build"].as_string() == "test");

  const std::string state = request(port, "GET", "/api/state", token);
  assert(state.find("200 OK") != std::string::npos);
  const Json state_json = Json::parse(body_of(state));
  assert(state_json["mode"].as_string() == "full");
  assert(state_json["tools"].is_array());
  assert(state_json["context"]["limit"].as_number() == 8000);
  assert(state_json["home_tile"].is_object());
  assert(state_json["home_tile"]["title_id"].as_string() == "HARN00001");
  assert(state_json["home_tile"]["state"].as_string() == "pending");
  assert(state_json["home_tile"]["deeplink"].as_string() ==
         "http://127.0.0.1:" + std::to_string(port) + "/");

  const std::string health = request(port, "GET", "/api/health", token);
  assert(health.find("200 OK") != std::string::npos);
  const Json health_json = Json::parse(body_of(health));
  assert(health_json["ok"].as_bool());
  assert(health_json["home_tile"]["title_id"].as_string() == "HARN00001");
  const long long generation_before_resume =
      health_json["listener_generation"].as_number();
  assert(generation_before_resume == 1);

  const std::string model_get = request(port, "GET", "/api/model", token);
  assert(model_get.find("200 OK") != std::string::npos);
  const Json model_get_json = Json::parse(body_of(model_get));
  assert(model_get_json["ok"].as_bool());
  assert(model_get_json["current"]["model"].as_string() == "stub");
  assert(model_get_json["presets"].is_array());
  assert(model_get_json["presets"].size() >= 5);
  assert(model_get_json["presets"][0]["base_url"].is_string());
  assert(model_get_json["presets"][0]["local"].as_bool(false));
  assert(model_get_json["current"]["local"].as_bool(true) == false);

  const std::string model_bad = request(
      port, "POST", "/api/model", token,
      "{\"base_url\":\"file:///tmp\",\"model\":\"bad\"}");
  assert(model_bad.find("400 Bad Request") != std::string::npos);
  assert(body_of(model_bad).find("URL policy") != std::string::npos);

  const std::string model_set = request(
      port, "POST", "/api/model", token,
      "{\"base_url\":\"http://127.0.0.1:9/v1\",\"model\":\"switched\","
      "\"temperature\":0.4,\"max_tokens\":64}");
  assert(model_set.find("200 OK") != std::string::npos);
  const Json model_set_json = Json::parse(body_of(model_set));
  assert(model_set_json["ok"].as_bool());
  assert(model_set_json["current"]["model"].as_string() == "switched");
  assert(model_set_json["current"]["max_tokens"].as_number() == 64);

  const std::string state_after_model = request(port, "GET", "/api/state", token);
  const Json state_after_model_json = Json::parse(body_of(state_after_model));
  assert(state_after_model_json["model"].as_string() == "switched");
  assert(state_after_model_json["provider"]["base_url"].as_string() ==
         "http://127.0.0.1:9/v1");

  const std::string local_bad = request(
      port, "POST", "/api/model", token,
      "{\"local\":true,\"base_url\":\"http://192.168.4.100:8081/v1\","
      "\"local_model_path\":\"/data/homebrew/model.gguf\","
      "\"model\":\"bad-local\"}");
  assert(local_bad.find("400 Bad Request") != std::string::npos);
  assert(body_of(local_bad).find("local model endpoint") != std::string::npos);

  const std::string local_set = request(
      port, "POST", "/api/model", token,
      "{\"local\":true,\"base_url\":\"http://127.0.0.1:8081/v1\","
      "\"local_model_path\":\"/data/homebrew/model.gguf\","
      "\"model\":\"qwen3.5-0.8b\",\"api_key_env\":\"\"}");
  assert(local_set.find("200 OK") != std::string::npos);
  const Json local_set_json = Json::parse(body_of(local_set));
  assert(local_set_json["ok"].as_bool());
  assert(local_set_json["current"]["local"].as_bool(false));
  assert(local_set_json["current"]["base_url"].as_string() ==
         "http://127.0.0.1:8081/v1");

  const std::string local_restore = request(
      port, "POST", "/api/model", token,
      "{\"local\":false,\"base_url\":\"http://127.0.0.1:9/v1\","
      "\"model\":\"switched\",\"temperature\":0.4,\"max_tokens\":64}");
  assert(local_restore.find("200 OK") != std::string::npos);

  // The page's focus and the agent's surface are one state. A reported control
  // moves the surface's panel and focus; an unknown id is refused; and a
  // person's focus is not the agent driving.
  const std::string ui_initial = request(port, "GET", "/api/ui", token);
  assert(ui_initial.find("200 OK") != std::string::npos);
  assert(Json::parse(body_of(ui_initial))["panel"].as_string() == "activity");
  const std::string ui_focus =
      request(port, "POST", "/api/ui/focus", token, "{\"focus\":\"model:id\"}");
  assert(ui_focus.find("200 OK") != std::string::npos);
  const Json ui_focus_json = Json::parse(body_of(ui_focus));
  assert(ui_focus_json["ok"].as_bool(false));
  assert(ui_focus_json["panel"].as_string() == "model");
  assert(ui_focus_json["focus"].as_string() == "model:id");
  const std::string ui_unknown =
      request(port, "POST", "/api/ui/focus", token, "{\"focus\":\"nope\"}");
  assert(ui_unknown.find("400 Bad Request") != std::string::npos);
  const std::string ui_after = request(port, "GET", "/api/ui", token);
  const Json ui_after_json = Json::parse(body_of(ui_after));
  assert(ui_after_json["panel"].as_string() == "model");
  assert(ui_after_json["focus"].as_string() == "model:id");
  assert(ui_after_json["driving"].as_bool(true) == false);

  const std::string chat = request(
      port, "POST", "/api/chat", token,
      "{\"message\":\"server round trip\"}");
  assert(chat.find("200 OK") != std::string::npos);
  const Json chat_json = Json::parse(body_of(chat));
  assert(chat_json["ok"].as_bool(false));
  assert(chat_json["answer"].as_string() == "hello from switched");
  assert(chat_json["usage"]["total_tokens"].as_number() == 5);

  const std::string history = request(port, "GET", "/api/history", token);
  assert(history.find("200 OK") != std::string::npos);
  const Json history_json = Json::parse(body_of(history));
  assert(history_json["messages"].size() == 3);  // system, user, assistant

  const std::string unauth_shutdown =
      request(port, "POST", "/api/shutdown", "");
  assert(unauth_shutdown.find("401 Unauthorized") != std::string::npos);

  // Rest mode is a freeze, not a clean close. SIGCONT is one resume signal a
  // PS5 payload may receive; the server also watches the wall clock and the
  // non-loopback IPv4 address set. Hold the simulated network down across the
  // resume:
  // the old listeners must keep serving while the supervisor waits, the
  // generation must not advance until the network is back, and the bind must
  // then succeed. The gate is a test-only server.cpp hook.
  set_network_gate(network_gate, true);
  assert(::kill(child, SIGCONT) == 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  for (int i = 0; i < 8; ++i) {
    const std::string during =
        request(port, "GET", "/api/health", token, std::string(), true);
    assert(during.find("200 OK") != std::string::npos);
    const Json during_json = Json::parse(body_of(during));
    assert(during_json["listener_generation"].as_number() ==
           generation_before_resume);
    assert(Json::parse(udp_discover(discovery_port))["http_port"].as_number() ==
           port);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }

  set_network_gate(network_gate, false);
  const long long generation_after_first =
      wait_for_generation(port, token, generation_before_resume);
  const std::string resumed = request(port, "GET", "/api/health", token);
  assert(resumed.find("200 OK") != std::string::npos);
  assert(Json::parse(body_of(resumed))["listener_generation"].as_number() ==
         generation_after_first);
  assert(Json::parse(udp_discover(discovery_port))["http_port"].as_number() == port);
  const std::string resumed_history = request(port, "GET", "/api/history", token);
  assert(resumed_history.find("200 OK") != std::string::npos);
  assert(Json::parse(body_of(resumed_history))["messages"].size() == 3);

  // A second wake must not stay pinned behind the first attempt's retry. Keep
  // the network down while the signal arrives, then restore it and require a
  // second successful generation.
  set_network_gate(network_gate, true);
  assert(::kill(child, SIGCONT) == 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  const std::string second_wake =
      request(port, "GET", "/api/health", token, std::string(), true);
  assert(second_wake.find("200 OK") != std::string::npos);
  assert(Json::parse(body_of(second_wake))["listener_generation"].as_number() ==
         generation_after_first);
  set_network_gate(network_gate, false);
  const long long generation_after_second =
      wait_for_generation(port, token, generation_after_first);
  assert(generation_after_second > generation_after_first);

  const std::string shutdown = request(port, "POST", "/api/shutdown", token);
  assert(shutdown.find("200 OK") != std::string::npos);
  int status = 0;
  bool exited = false;
  for (int i = 0; i < 50; ++i) {
    const pid_t result = ::waitpid(child, &status, WNOHANG);
    if (result == child) {
      exited = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  assert(exited);
  std::printf("server_test: ok (port %d)\n", port);
  return 0;
}
