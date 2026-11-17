// End-to-end provider round trip.
//
// A local OpenAI-compatible stub serves a real two-call conversation, and the
// real code paths do the work: the CLI binary with its config parser and path
// guards, the agent loop, the HTTP client, the URL policy, tool dispatch with
// its deadline, and secret redaction. Nothing here reimplements the loop.
//
// This file is only built with -DPS5H_TEST_HOOKS, which is what makes the
// cleartext-loopback policy available. The production binary is built without
// it, and the last test proves that binary rejects both the cleartext endpoint
// and the hidden flag.

#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cassert>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ps5h/agent.h"
#include "ps5h/core.h"
#include "ps5h/json.h"
#include "ps5h/policy.h"
#include "ps5h/secrets.h"

using namespace ps5h;

namespace {

// --- helpers -----------------------------------------------------------------

std::string make_temp_dir() {
  char pattern[] = "/tmp/ps5h-e2e-XXXXXX";
  char* made = ::mkdtemp(pattern);
  assert(made != nullptr);
  return std::string(made);
}

void write_text(const std::string& path, const std::string& text) {
  FILE* file = std::fopen(path.c_str(), "wb");
  assert(file != nullptr);
  std::fwrite(text.data(), 1, text.size(), file);
  std::fclose(file);
}

std::string quote(const std::string& value) {
  std::string out = "'";
  for (char c : value) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
}

struct CommandResult {
  int exit_code = -1;
  std::string output;
};

CommandResult run_command(const std::string& command) {
  CommandResult result;
  FILE* pipe = ::popen((command + " 2>&1").c_str(), "r");
  if (pipe == nullptr) {
    return result;
  }
  char buffer[2048];
  std::size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof buffer, pipe)) > 0) {
    result.output.append(buffer, read);
  }
  const int status = ::pclose(pipe);
  result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}

// --- stub provider -----------------------------------------------------------

struct Reply {
  std::string body;
  bool truncate = false;  // advertise more bytes than are sent
};

struct Recorded {
  std::string method;
  std::string target;
  std::string body;
  std::map<std::string, std::string> headers;
};

class StubProvider {
 public:
  explicit StubProvider(std::vector<Reply> replies) : replies_(std::move(replies)) {
    listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(listener_ >= 0);
    int yes = 1;
    ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    assert(::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0);
    assert(::listen(listener_, 8) == 0);
    socklen_t length = sizeof address;
    assert(::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length) == 0);
    port_ = ntohs(address.sin_port);
    thread_ = std::thread([this]() { serve(); });
  }

  ~StubProvider() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    ::close(listener_);
  }

  std::string base_url() const { return "http://127.0.0.1:" + std::to_string(port_); }

  bool wait_for_requests(std::size_t count, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (requests_.size() >= count) return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return requests_.size() >= count;
  }

  std::size_t request_count() {
    std::lock_guard<std::mutex> lock(mutex_);
    return requests_.size();
  }

  Recorded request(std::size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    assert(index < requests_.size());
    return requests_[index];
  }

 private:
  bool read_request(int client, Recorded* out) {
    std::string raw;
    char buffer[4096];
    while (raw.find("\r\n\r\n") == std::string::npos) {
      const ssize_t received = ::recv(client, buffer, sizeof buffer, 0);
      if (received <= 0) return false;
      raw.append(buffer, static_cast<std::size_t>(received));
      if (raw.size() > (1u << 20)) return false;
    }
    const std::size_t header_end = raw.find("\r\n\r\n") + 4;

    const std::size_t line_end = raw.find("\r\n");
    const std::string request_line = raw.substr(0, line_end);
    const std::size_t first_space = request_line.find(' ');
    if (first_space == std::string::npos) return false;
    const std::size_t second_space = request_line.find(' ', first_space + 1);
    out->method = request_line.substr(0, first_space);
    out->target = request_line.substr(first_space + 1,
                                      second_space == std::string::npos
                                          ? std::string::npos
                                          : second_space - first_space - 1);

    std::size_t content_length = 0;
    std::size_t position = line_end + 2;
    while (position < header_end) {
      const std::size_t next = raw.find("\r\n", position);
      if (next == std::string::npos || next >= header_end) break;
      std::string line = raw.substr(position, next - position);
      const std::size_t colon = line.find(':');
      if (colon != std::string::npos) {
        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());
        for (char& c : name) {
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        out->headers[name] = value;
        if (name == "content-length") {
          content_length = static_cast<std::size_t>(std::strtoul(value.c_str(), nullptr, 10));
        }
      }
      position = next + 2;
    }

    while (raw.size() < header_end + content_length) {
      const ssize_t received = ::recv(client, buffer, sizeof buffer, 0);
      if (received <= 0) break;
      raw.append(buffer, static_cast<std::size_t>(received));
    }
    out->body = raw.substr(header_end, content_length);
    return true;
  }

  void serve() {
    while (!stop_) {
      pollfd probe{listener_, POLLIN, 0};
      if (::poll(&probe, 1, 50) <= 0) {
        continue;
      }
      const int client = ::accept(listener_, nullptr, nullptr);
      if (client < 0) {
        continue;
      }
      Recorded recorded;
      if (!read_request(client, &recorded)) {
        ::close(client);
        continue;
      }

      Reply reply;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        requests_.push_back(recorded);
        if (cursor_ < replies_.size()) {
          reply = replies_[cursor_++];
        } else {
          reply.body = "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"no script\"}}]}";
        }
      }

      const std::string head =
          "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
          std::to_string(reply.body.size() + (reply.truncate ? 512 : 0)) +
          "\r\nConnection: close\r\n\r\n";
      ::send(client, head.data(), head.size(), 0);
      if (!reply.body.empty()) {
        ::send(client, reply.body.data(), reply.body.size(), 0);
      }
      ::close(client);
    }
  }

  std::mutex mutex_;
  std::vector<Recorded> requests_;
  std::vector<Reply> replies_;
  std::size_t cursor_ = 0;
  bool stop_ = false;
  int listener_ = -1;
  int port_ = 0;
  std::thread thread_;
};

// --- conversation fixtures ---------------------------------------------------

std::string tool_call_reply(const std::string& call_id, const std::string& tool,
                            const Json& arguments) {
  Json function = Json::object();
  function["name"] = tool;
  function["arguments"] = arguments.dump();

  Json call = Json::object();
  call["id"] = call_id;
  call["type"] = "function";
  call["function"] = function;

  Json calls = Json::array();
  calls.push_back(call);

  Json message = Json::object();
  message["role"] = "assistant";
  message["content"] = Json();
  message["tool_calls"] = calls;

  Json choice = Json::object();
  choice["index"] = 0;
  choice["finish_reason"] = "tool_calls";
  choice["message"] = message;

  Json choices = Json::array();
  choices.push_back(choice);

  Json reply = Json::object();
  reply["id"] = "stub-1";
  reply["object"] = "chat.completion";
  reply["choices"] = choices;
  return reply.dump();
}

std::string final_reply(const std::string& text) {
  Json message = Json::object();
  message["role"] = "assistant";
  message["content"] = text;

  Json choice = Json::object();
  choice["index"] = 0;
  choice["finish_reason"] = "stop";
  choice["message"] = message;

  Json choices = Json::array();
  choices.push_back(choice);

  Json reply = Json::object();
  reply["id"] = "stub-2";
  reply["object"] = "chat.completion";
  reply["choices"] = choices;
  return reply.dump();
}

// --- tests -------------------------------------------------------------------

// The production default must reject the stub outright. This is the proof that
// the round trip's bypass is opt-in, not a loosened default.
void test_default_policy_rejects_stub() {
  StubProvider stub({Reply{"{}"}});
  UrlPolicy policy;  // exactly what the CLI builds from a normal config
  const auto cleartext = policy.check(stub.base_url() + "/chat/completions");
  assert(!cleartext.allowed);
  assert(cleartext.reason.find("scheme") != std::string::npos);

  const auto loopback_https = policy.check("https://127.0.0.1:8443/chat/completions");
  assert(!loopback_https.allowed);
  assert(loopback_https.reason.find("private") != std::string::npos);

  const auto lan = policy.check("https://192.168.1.10/v1/chat/completions");
  assert(!lan.allowed);
}

// The whole conversation, through the CLI binary: config parsing, path guards,
// tool dispatch, deadline, redaction, HTTP client, policy, agent loop.
void test_cli_round_trip() {
  const std::string directory = make_temp_dir();
  const std::string secret = "sk-e2e-secret-value-abcdef";
  ::setenv("PS5H_E2E_KEY", secret.c_str(), 1);

  const std::string library = directory + "/library";
  assert(::mkdir(library.c_str(), 0700) == 0);
  const std::string notes = library + "/notes.txt";
  write_text(notes, "the passphrase is " + secret + "\n");

  Json model = Json::object();
  model["base_url"] = "https://api.openai.com/v1";
  model["model"] = "stub-model";
  model["api_key_env"] = "PS5H_E2E_KEY";

  Json roots = Json::array();
  roots.push_back(directory);
  Json secret_env = Json::array();
  secret_env.push_back("PS5H_E2E_KEY");
  Json allowed_hosts = Json::array();
  Json url_policy = Json::object();
  url_policy["https_only"] = true;
  url_policy["allowed_hosts"] = allowed_hosts;

  Json config = Json::object();
  config["model"] = model;
  config["endpoints_dir"] = "";
  config["plugins_dir"] = "";
  config["max_steps"] = 4;
  config["allowed_roots"] = roots;
  config["read_roots"] = roots;
  config["secret_env"] = secret_env;
  // A normal config must not be able to turn the test hook on.
  config["url_policy"] = url_policy;

  const std::string config_path = directory + "/config.json";
  write_text(config_path, config.dump(2));

  Json arguments = Json::object();
  arguments["path"] = notes;

  StubProvider stub({Reply{tool_call_reply("call_abc", "fs_read", arguments)},
                     Reply{final_reply("round trip complete")}});

  const std::string command = "./build/harness_e2e --config " + quote(config_path) +
                              " --base-url " + quote(stub.base_url()) +
                              " --test-allow-loopback-cleartext " + quote("read the notes");
  const CommandResult result = run_command(command);
  if (result.exit_code != 0) {
    std::fprintf(stderr, "CLI exited %d:\n%s\n", result.exit_code, result.output.c_str());
  }

  assert(result.exit_code == 0);
  assert(result.output.find("round trip complete") != std::string::npos);
  assert(stub.wait_for_requests(2, 8000));
  assert(stub.request_count() == 2);

  const Recorded first = stub.request(0);
  assert(first.target == "/chat/completions");
  assert(first.method == "POST");
  assert(first.headers.count("authorization") == 1);
  assert(first.headers.at("authorization") == "Bearer " + secret);
  assert(first.body.find("\"fs_read\"") != std::string::npos);
  assert(first.body.find("\"tools\"") != std::string::npos);

  const Recorded second = stub.request(1);
  assert(second.body.find("\"role\":\"tool\"") != std::string::npos);
  assert(second.body.find("\"tool_call_id\":\"call_abc\"") != std::string::npos);
  // Redaction ran inside the real agent before the message reached the provider.
  assert(second.body.find("[redacted]") != std::string::npos);
  assert(second.body.find(secret) == std::string::npos);
}

Agent make_agent(const StubProvider& stub, ToolRegistry& registry, LogFn log,
                 const std::string& key_env = std::string()) {
  ModelConfig config;
  config.base_url = stub.base_url();
  config.model = "stub-model";
  if (!key_env.empty()) {
    config.api_key_env = key_env;
  }
  EnvFn env = [key_env](const std::string& name) -> std::string {
    if (!key_env.empty() && name == key_env) {
      const char* value = std::getenv(key_env.c_str());
      return value != nullptr ? value : std::string();
    }
    return std::string();
  };
  return Agent(config, make_http_client(), registry, env, log,
               [](const std::string&) {}, UrlPolicy::test_only_loopback_policy(), SecretSet{});
}

void test_malformed_provider_response() {
  StubProvider stub({Reply{"this is not json"}});
  ToolRegistry registry;
  std::string log_text;
  Agent agent = make_agent(stub, registry, [&](const std::string& m) { log_text += m + "\n"; });
  AgentOptions options;
  options.max_steps = 3;
  options.system_prompt = "test";

  const bool ok = agent.run("hello", options);
  assert(!ok);
  assert(log_text.find("invalid JSON") != std::string::npos);
}

void test_unknown_tool_is_a_clean_tool_error() {
  Json arguments = Json::object();
  arguments["value"] = "x";
  StubProvider stub({Reply{tool_call_reply("call_missing", "definitely_not_a_tool", arguments)},
                     Reply{final_reply("recovered")}});
  ToolRegistry registry;
  std::string log_text;
  Agent agent = make_agent(stub, registry, [&](const std::string& m) { log_text += m + "\n"; });
  AgentOptions options;
  options.max_steps = 4;
  options.system_prompt = "test";

  const bool ok = agent.run("go", options);
  assert(ok);
  assert(stub.wait_for_requests(2, 5000));
  const Recorded second = stub.request(1);
  assert(second.body.find("unknown tool: definitely_not_a_tool") != std::string::npos);
}

void test_truncated_provider_response() {
  StubProvider stub({Reply{"{\"choices\":[{\"message\":{\"content\":\"cut", true}});
  ToolRegistry registry;
  std::string log_text;
  Agent agent = make_agent(stub, registry, [&](const std::string& m) { log_text += m + "\n"; });
  AgentOptions options;
  options.max_steps = 3;
  options.system_prompt = "test";

  const bool ok = agent.run("hello", options);
  assert(!ok);
  assert(log_text.find("model request failed") != std::string::npos);
}

// A hung tool must not wedge the loop: the deadline returns and the abandoned
// worker keeps running on its own copy.
void test_deadline_returns_without_blocking() {
  Tool slow;
  slow.name = "slow_tool";
  slow.description = "sleeps";
  slow.timeout_ms = 80;
  slow.parameters = Json::object();
  slow.parameters["type"] = "object";
  slow.parameters["properties"] = Json::object();
  slow.handler = [](const Json&) -> Json {
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    return Json("late");
  };

  ToolRegistry registry;
  registry.add(slow);

  Json arguments = Json::object();
  StubProvider stub({Reply{tool_call_reply("call_slow", "slow_tool", arguments)},
                     Reply{final_reply("after timeout")}});
  std::string log_text;
  Agent agent = make_agent(stub, registry, [&](const std::string& m) { log_text += m + "\n"; });
  AgentOptions options;
  options.max_steps = 4;
  options.system_prompt = "test";

  const auto started = std::chrono::steady_clock::now();
  const bool ok = agent.run("go", options);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  assert(ok);
  assert(stub.wait_for_requests(2, 5000));
  const Recorded second = stub.request(1);
  assert(second.body.find("timed out after 80 ms") != std::string::npos);
  assert(elapsed.count() < 400);  // std::async would have blocked ~600 ms
}

// The shipped binary has no hook at all: no cleartext endpoint, no hidden flag.
void test_production_binary_has_no_bypass() {
  StubProvider stub({Reply{"{}"}});

  const CommandResult cleartext = run_command("./build/harness --base-url " + quote(stub.base_url()) +
                                              " --prompt hello");
  assert(cleartext.exit_code != 0);
  assert(cleartext.output.find("URL policy") != std::string::npos);
  assert(stub.request_count() == 0);

  const CommandResult flag = run_command("./build/harness --test-allow-loopback-cleartext --prompt hello");
  assert(flag.exit_code != 0);
  assert(flag.output.find("unknown option") != std::string::npos);
}

}  // namespace

int main() {
  std::signal(SIGPIPE, SIG_IGN);
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  std::printf("default policy rejects stub...\n");
  test_default_policy_rejects_stub();
  std::printf("cli round trip...\n");
  test_cli_round_trip();
  std::printf("malformed provider response...\n");
  test_malformed_provider_response();
  std::printf("unknown tool...\n");
  test_unknown_tool_is_a_clean_tool_error();
  std::printf("truncated response...\n");
  test_truncated_provider_response();
  std::printf("deadline...\n");
  test_deadline_returns_without_blocking();
  std::printf("production binary...\n");
  test_production_binary_has_no_bypass();

  std::printf("e2e provider tests passed\n");
  return 0;
}
