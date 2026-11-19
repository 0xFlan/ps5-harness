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

#include <atomic>
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

std::string multi_tool_call_reply() {
  auto make_call = [](const char* id, const char* name) {
    Json function = Json::object();
    function["name"] = name;
    function["arguments"] = "{}";
    Json call = Json::object();
    call["id"] = id;
    call["type"] = "function";
    call["function"] = function;
    return call;
  };

  Json calls = Json::array();
  calls.push_back(make_call("call_a", "wait_a"));
  calls.push_back(make_call("call_b", "wait_b"));

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
  reply["id"] = "stub-parallel";
  reply["object"] = "chat.completion";
  reply["choices"] = choices;
  return reply.dump();
}

std::string final_reply(const std::string& text, long long total_tokens = 0) {
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
  if (total_tokens > 0) {
    Json usage = Json::object();
    usage["prompt_tokens"] = total_tokens / 2;
    usage["completion_tokens"] = total_tokens - total_tokens / 2;
    usage["total_tokens"] = total_tokens;
    reply["usage"] = usage;
  }
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
  // The user turn reached the provider.
  assert(first.body.find("read the notes") != std::string::npos);

  const Recorded second = stub.request(1);
  // History is intact: the assistant turn that requested the tool is resent.
  assert(second.body.find("\"role\":\"assistant\"") != std::string::npos);
  assert(second.body.find("\"role\":\"tool\"") != std::string::npos);
  assert(second.body.find("\"tool_call_id\":\"call_abc\"") != std::string::npos);
  // Redaction ran inside the real agent before the message reached the provider.
  assert(second.body.find("[redacted]") != std::string::npos);
  assert(second.body.find(secret) == std::string::npos);
}

// A profile staged in a local directory must reach the provider request and
// the informational commands must work without a model call.
void test_local_agent_profile() {
  const std::string directory = make_temp_dir();
  const std::string agents = directory + "/agents";
  assert(::mkdir(agents.c_str(), 0700) == 0);
  write_text(agents + "/archivist.json",
             R"({
  "description": "Local test agent",
  "system_prompt": "LOCAL PROFILE PROMPT",
  "model": "profile-model",
  "temperature": 0.4,
  "max_tokens": 123,
  "max_steps": 3
})");

  Json model = Json::object();
  model["base_url"] = "https://api.openai.com/v1";
  model["model"] = "base-model";
  model["api_key_env"] = "PS5H_E2E_KEY";
  Json roots = Json::array();
  roots.push_back(directory);
  Json config = Json::object();
  config["model"] = model;
  config["endpoints_dir"] = "";
  config["plugins_dir"] = "";
  config["max_steps"] = 4;
  config["allowed_roots"] = roots;
  config["read_roots"] = roots;
  config["secret_env"] = Json::array();
  const std::string config_path = directory + "/config.json";
  write_text(config_path, config.dump(2));

  StubProvider stub({Reply{final_reply("local agent done")}});
  const std::string command =
      "./build/harness_e2e --config " + quote(config_path) + " --agents " +
      quote(agents) + " --agent archivist --base-url " +
      quote(stub.base_url()) + " --test-allow-loopback-cleartext " +
      quote("hello");
  const CommandResult result = run_command(command);
  if (result.exit_code != 0) {
    std::fprintf(stderr, "local agent CLI exited %d:\n%s\n", result.exit_code,
                 result.output.c_str());
  }
  assert(result.exit_code == 0);
  assert(stub.wait_for_requests(1, 8000));
  const Recorded first = stub.request(0);
  assert(first.body.find("LOCAL PROFILE PROMPT") != std::string::npos);
  assert(first.body.find("\"model\":\"profile-model\"") != std::string::npos);
  assert(first.body.find("\"max_tokens\":123") != std::string::npos);
  assert(first.body.find("\"temperature\":0.4") != std::string::npos);

  const CommandResult info =
      run_command("./build/harness_e2e --agents " + quote(agents) +
                  " --agent archivist --agent-info");
  assert(info.exit_code == 0);
  assert(info.output.find("LOCAL PROFILE PROMPT") != std::string::npos);
  assert(info.output.find("model=profile-model") != std::string::npos);
  assert(info.output.find("max_steps=3") != std::string::npos);

  const CommandResult list = run_command(
      "./build/harness_e2e --agents " + quote(agents) + " --list-agents");
  assert(list.exit_code == 0);
  assert(list.output.find("archivist") != std::string::npos);
  assert(list.output.find("Local test agent") != std::string::npos);

  const CommandResult missing =
      run_command("./build/harness_e2e --agents " + quote(agents) +
                  " --agent nope --agent-info");
  assert(missing.exit_code != 0);
  assert(missing.output.find("cannot load agent") != std::string::npos);
}

// A console loader may not be able to set environment variables. The CLI must
// accept a key file without putting the secret in the config or its output.
void test_api_key_file() {
  const std::string directory = make_temp_dir();
  const std::string key = "sk-file-secret-value-123456";
  ::unsetenv("PS5H_E2E_FILE_KEY");
  const std::string key_path = directory + "/api-key";
  write_text(key_path, "  " + key + "\n");

  Json model = Json::object();
  model["base_url"] = "https://api.openai.com/v1";
  model["model"] = "stub-model";
  model["api_key_env"] = "PS5H_E2E_FILE_KEY";
  model["api_key_file"] = key_path;
  Json roots = Json::array();
  roots.push_back(directory);
  Json config = Json::object();
  config["model"] = model;
  config["endpoints_dir"] = "";
  config["plugins_dir"] = "";
  config["max_steps"] = 2;
  config["allowed_roots"] = roots;
  config["read_roots"] = roots;
  config["secret_env"] = Json::array();
  const std::string config_path = directory + "/config.json";
  write_text(config_path, config.dump(2));

  StubProvider stub({Reply{final_reply("key file works")}});
  const std::string command =
      "./build/harness_e2e --config " + quote(config_path) +
      " --base-url " + quote(stub.base_url()) +
      " --test-allow-loopback-cleartext " + quote("say hello");
  const CommandResult result = run_command(command);
  if (result.exit_code != 0) {
    std::fprintf(stderr, "key-file CLI exited %d:\n%s\n", result.exit_code,
                 result.output.c_str());
  }
  assert(result.exit_code == 0);
  assert(result.output.find("key file works") != std::string::npos);
  assert(result.output.find(key) == std::string::npos);
  assert(stub.wait_for_requests(1, 8000));
  const Recorded first = stub.request(0);
  assert(first.headers.count("authorization") == 1);
  assert(first.headers.at("authorization") == "Bearer " + key);
}

std::string write_delegation_config(const std::string& directory, int max_steps,
                                    int max_tokens, int max_depth) {
  Json model = Json::object();
  model["base_url"] = "https://api.openai.com/v1";
  model["model"] = "parent-model";
  model["api_key_env"] = "PS5H_E2E_KEY";
  model["max_tokens"] = max_tokens;

  Json roots = Json::array();
  roots.push_back(directory);
  Json config = Json::object();
  config["model"] = model;
  config["endpoints_dir"] = "";
  config["plugins_dir"] = "";
  config["max_steps"] = max_steps;
  config["delegation_max_depth"] = max_depth;
  config["allowed_roots"] = roots;
  config["read_roots"] = roots;
  config["secret_env"] = Json::array();
  const std::string path = directory + "/config.json";
  write_text(path, config.dump(2));
  return path;
}

void write_profile(const std::string& agents, const std::string& name,
                   const std::string& body) {
  write_text(agents + "/" + name + ".json", body);
}

std::string read_text_file(const std::string& path) {
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

// The built-in skill and fs_edit tools, through the real CLI: listing, loading,
// bundled resources, containment and a targeted file edit. This is the same
// registry every profile and delegated child receives.
void test_skill_and_edit_tools() {
  const std::string directory = make_temp_dir();
  const std::string skills = directory + "/skills";
  const std::string skill = skills + "/console-notes";
  assert(::mkdir(skills.c_str(), 0700) == 0);
  assert(::mkdir(skill.c_str(), 0700) == 0);
  assert(::mkdir((skill + "/templates").c_str(), 0700) == 0);
  write_text(skill + "/SKILL.md",
             "---\n"
             "name: console-notes\n"
             "description: Notes for the console\n"
             "---\n\n"
             "# Console notes\n\n"
             "Write a note.\n");
  write_text(skill + "/templates/note.md", "NOTE TEMPLATE\n");
  write_text(skill + "/credentials.txt", "SECRET\n");
  const std::string outside = directory + "-outside.txt";
  write_text(outside, "OUTSIDE\n");
  write_text(directory + "/skill-escape.txt", "PARENT\n");
  assert(::symlink(outside.c_str(), (skill + "/leak.txt").c_str()) == 0);

  const std::string target = directory + "/notes.txt";
  write_text(target, "alpha old omega\n");
  const std::string config_path = write_delegation_config(directory, 4, 500, 0);
  const std::string base = "./build/harness_e2e --config " +
                           quote(config_path) + " --skills " + quote(skills);
  auto call = [&](const std::string& tool, const Json& arguments) {
    return run_command(base + " --call " + tool + " --args " +
                       quote(arguments.dump()));
  };

  const CommandResult tools = run_command(base + " --tools");
  assert(tools.exit_code == 0);
  assert(tools.output.find("fs_write") != std::string::npos);
  assert(tools.output.find("fs_edit") != std::string::npos);
  assert(tools.output.find("skill") != std::string::npos);

  const CommandResult listed = call("skill", Json::object());
  assert(listed.exit_code == 0);
  assert(listed.output.find("console-notes") != std::string::npos);
  assert(listed.output.find("Notes for the console") != std::string::npos);

  Json load_args = Json::object();
  load_args["name"] = "console-notes";
  const CommandResult loaded = call("skill", load_args);
  assert(loaded.exit_code == 0);
  assert(loaded.output.find("# Console notes") != std::string::npos);
  assert(loaded.output.find("templates/note.md") != std::string::npos);

  Json resource_args = Json::object();
  resource_args["name"] = "console-notes";
  resource_args["resource"] = "templates/note.md";
  const CommandResult resource = call("skill", resource_args);
  assert(resource.exit_code == 0);
  assert(resource.output.find("NOTE TEMPLATE") != std::string::npos);

  Json escape_args = Json::object();
  escape_args["name"] = "console-notes";
  escape_args["resource"] = "../../skill-escape.txt";
  const CommandResult escaped = call("skill", escape_args);
  assert(escaped.exit_code == 0);
  assert(escaped.output.find("outside the skill directory") !=
         std::string::npos);

  Json link_args = Json::object();
  link_args["name"] = "console-notes";
  link_args["resource"] = "leak.txt";
  const CommandResult linked = call("skill", link_args);
  assert(linked.output.find("outside the skill directory") !=
         std::string::npos);
  assert(linked.output.find("OUTSIDE") == std::string::npos);

  Json secret_args = Json::object();
  secret_args["name"] = "console-notes";
  secret_args["resource"] = "credentials.txt";
  const CommandResult secret = call("skill", secret_args);
  assert(secret.output.find("denied") != std::string::npos);

  Json edit_args = Json::object();
  edit_args["path"] = target;
  edit_args["old"] = "old";
  edit_args["new"] = "new";
  const CommandResult edited = call("fs_edit", edit_args);
  assert(edited.exit_code == 0);
  assert(edited.output.find("replaced") != std::string::npos);
  assert(read_text_file(target) == "alpha new omega\n");

  write_text(target, "x x\n");
  Json ambiguous_args = Json::object();
  ambiguous_args["path"] = target;
  ambiguous_args["old"] = "x";
  ambiguous_args["new"] = "y";
  const CommandResult ambiguous = call("fs_edit", ambiguous_args);
  assert(ambiguous.output.find("appears 2 times") != std::string::npos);
  assert(read_text_file(target) == "x x\n");

  ambiguous_args["replace_all"] = true;
  const CommandResult replaced = call("fs_edit", ambiguous_args);
  assert(replaced.exit_code == 0);
  assert(read_text_file(target) == "y y\n");

  Json outside_args = Json::object();
  outside_args["path"] = "/etc/hosts";
  outside_args["old"] = "localhost";
  outside_args["new"] = "x";
  const CommandResult outside_edit = call("fs_edit", outside_args);
  assert(outside_edit.output.find("outside the write roots") !=
         std::string::npos);
}

// A real parent conversation calls delegate; the stub then serves the child
// conversation and the parent sees the child's answer as a tool result.
void test_delegation_round_trip() {
  const std::string directory = make_temp_dir();
  const std::string agents = directory + "/agents";
  assert(::mkdir(agents.c_str(), 0700) == 0);
  write_text(agents + "/helper.md", "HELPER PROMPT FROM FILE\n");
  write_profile(agents, "helper",
                R"({"description":"Helper","system_prompt_file":"helper.md","model":"helper-model","max_steps":1,"max_tokens":200})");
  const std::string config_path = write_delegation_config(directory, 6, 500, 2);

  Json delegate_args = Json::object();
  delegate_args["profile"] = "helper";
  delegate_args["task"] = "summarise the local notes";
  Json read_args = Json::object();
  read_args["path"] = "/etc/hosts";
  StubProvider stub({Reply{tool_call_reply("call_delegate", "delegate", delegate_args)},
                     Reply{tool_call_reply("call_child_read", "fs_read", read_args)},
                     Reply{final_reply("helper result", 80)},
                     Reply{final_reply("parent result")}});

  const std::string command =
      "./build/harness_e2e --config " + quote(config_path) + " --agents " +
      quote(agents) + " --model forced-model --max-steps 4 --max-depth 2 "
      "--base-url " + quote(stub.base_url()) +
      " --test-allow-loopback-cleartext " + quote("parent task");
  const CommandResult result = run_command(command);
  if (result.exit_code != 0) {
    std::fprintf(stderr, "delegation CLI exited %d:\n%s\n", result.exit_code,
                 result.output.c_str());
  }

  assert(result.exit_code == 0);
  assert(result.output.find("parent result") != std::string::npos);
  assert(result.output.find("delegate -> profile=helper depth=1") !=
         std::string::npos);
  assert(result.output.find("delegate <- profile=helper ok=true") !=
         std::string::npos);
  assert(stub.wait_for_requests(4, 8000));
  assert(stub.request_count() == 4);

  const Recorded first = stub.request(0);
  assert(first.body.find("parent task") != std::string::npos);
  assert(first.body.find("delegate") != std::string::npos);

  const Recorded child = stub.request(1);
  assert(child.body.find("HELPER PROMPT FROM FILE") != std::string::npos);
  assert(child.body.find("summarise the local notes") != std::string::npos);
  assert(child.body.find("\"max_tokens\":200") != std::string::npos);
  // The child copy of the registry still carries the file-edit and skill tools.
  assert(child.body.find("\"fs_edit\"") != std::string::npos);
  assert(child.body.find("\"skill\"") != std::string::npos);
  // Explicit flags beat the helper profile's model and one-step cap: the child
  // completed a tool call and a final turn instead of stopping at one step.
  assert(child.body.find("\"model\":\"forced-model\"") != std::string::npos);
  assert(child.body.find("helper-model") == std::string::npos);

  // The child inherited the parent's read guard, so a path outside the parent
  // root is refused rather than becoming reachable through delegation.
  const Recorded child_after_read = stub.request(2);
  assert(child_after_read.body.find("outside the read roots") !=
         std::string::npos);
  assert(child_after_read.body.find("\"tool_call_id\":\"call_child_read\"") !=
         std::string::npos);

  const Recorded parent_after = stub.request(3);
  assert(parent_after.body.find("helper result") != std::string::npos);
  assert(parent_after.body.find("\"tool_call_id\":\"call_delegate\"") !=
         std::string::npos);
  assert(parent_after.body.find("remaining_steps") != std::string::npos);
  assert(parent_after.body.find("remaining_tokens") != std::string::npos);
}

// At max depth 1 the child may not create a grandchild. The refusal is a tool
// error inside the child conversation, not recursion or a relaxed limit.
void test_delegation_depth_limit() {
  const std::string directory = make_temp_dir();
  const std::string agents = directory + "/agents";
  assert(::mkdir(agents.c_str(), 0700) == 0);
  write_profile(agents, "helper",
                R"({"description":"Helper","system_prompt":"HELPER DEPTH PROMPT","max_steps":3,"max_tokens":100})");
  write_profile(agents, "grandchild",
                R"({"description":"Grandchild","system_prompt":"GRANDCHILD","max_steps":2})");
  const std::string config_path = write_delegation_config(directory, 6, 500, 1);

  Json first_args = Json::object();
  first_args["profile"] = "helper";
  first_args["task"] = "go deeper";
  Json nested_args = Json::object();
  nested_args["profile"] = "grandchild";
  nested_args["task"] = "too deep";
  StubProvider stub({Reply{tool_call_reply("call_outer", "delegate", first_args)},
                     Reply{tool_call_reply("call_nested", "delegate", nested_args)},
                     Reply{final_reply("child stopped at bound", 50)},
                     Reply{final_reply("parent depth done")}});

  const std::string command =
      "./build/harness_e2e --config " + quote(config_path) + " --agents " +
      quote(agents) + " --max-depth 1 --base-url " + quote(stub.base_url()) +
      " --test-allow-loopback-cleartext " + quote("parent depth task");
  const CommandResult result = run_command(command);
  if (result.exit_code != 0) {
    std::fprintf(stderr, "depth CLI exited %d:\n%s\n", result.exit_code,
                 result.output.c_str());
  }

  assert(result.exit_code == 0);
  assert(result.output.find("parent depth done") != std::string::npos);
  assert(result.output.find("delegation depth limit reached") !=
         std::string::npos);
  assert(stub.wait_for_requests(4, 8000));
  assert(stub.request_count() == 4);

  const Recorded child_first = stub.request(1);
  assert(child_first.body.find("HELPER DEPTH PROMPT") != std::string::npos);
  assert(child_first.body.find("delegate") != std::string::npos);

  const Recorded child_after = stub.request(2);
  assert(child_after.body.find("delegation depth limit reached") !=
         std::string::npos);
  assert(child_after.body.find("max 1") != std::string::npos);
  assert(child_after.body.find("\"tool_call_id\":\"call_nested\"") !=
         std::string::npos);
}

// The child reserves its full caps, then unused steps and reported tokens are
// refunded. 6-step/500-token parent pool minus one step and 80 tokens = 5/420.
void test_delegation_budget_accounting() {
  const std::string directory = make_temp_dir();
  const std::string agents = directory + "/agents";
  assert(::mkdir(agents.c_str(), 0700) == 0);
  write_profile(agents, "helper",
                R"({"description":"Helper","system_prompt":"BUDGET HELPER","max_steps":2,"max_tokens":200})");
  const std::string config_path = write_delegation_config(directory, 6, 500, 1);

  StubProvider stub({Reply{final_reply("budget child", 80)}});
  Json arguments = Json::object();
  arguments["profile"] = "helper";
  arguments["task"] = "work";
  const CommandResult result = run_command(
      "./build/harness_e2e --config " + quote(config_path) + " --agents " +
      quote(agents) + " --max-depth 1 --base-url " + quote(stub.base_url()) +
      " --test-allow-loopback-cleartext --call delegate --args " +
      quote(arguments.dump()));
  if (result.exit_code != 0) {
    std::fprintf(stderr, "budget CLI exited %d:\n%s\n", result.exit_code,
                 result.output.c_str());
  }

  assert(result.exit_code == 0);
  assert(stub.wait_for_requests(1, 8000));
  assert(stub.request_count() == 1);
  assert(result.output.find("\"ok\": true") != std::string::npos);
  assert(result.output.find("\"steps\": 1") != std::string::npos);
  assert(result.output.find("\"tokens\": 80") != std::string::npos);
  assert(result.output.find("\"remaining_steps\": 5") !=
         std::string::npos);
  assert(result.output.find("\"remaining_tokens\": 420") !=
         std::string::npos);
}

// Invalid profiles and self-delegation fail closed before any child runs; an
// over-large child request is refused by the shared step pool.
void test_delegation_refusals() {
  const std::string directory = make_temp_dir();
  const std::string agents = directory + "/agents";
  assert(::mkdir(agents.c_str(), 0700) == 0);
  write_profile(agents, "helper",
                R"({"description":"Helper","system_prompt":"SELF HELPER","max_steps":3})");
  write_profile(agents, "badroots",
                R"({"system_prompt":"x","allowed_roots":["/"]})");
  write_profile(agents, "badurl",
                R"({"system_prompt":"x","url_policy":{"https_only":true}})");
  write_profile(agents, "baddepth",
                R"({"system_prompt":"x","delegation_max_depth":8})");
  write_profile(agents, "greedy",
                R"({"system_prompt":"x","max_steps":12})");
  const std::string config_path = write_delegation_config(directory, 2, 500, 1);
  const std::string base = "./build/harness_e2e --config " + quote(config_path) +
                           " --agents " + quote(agents) + " --max-depth 1";

  auto delegate_call = [&](const std::string& profile) {
    Json arguments = Json::object();
    arguments["profile"] = profile;
    arguments["task"] = "x";
    return run_command(base + " --call delegate --args " +
                       quote(arguments.dump()));
  };

  const CommandResult missing = delegate_call("missing");
  assert(missing.exit_code == 0);
  assert(missing.output.find("cannot load agent") != std::string::npos);
  assert(missing.output.find("missing") != std::string::npos);

  const CommandResult roots = delegate_call("badroots");
  assert(roots.exit_code == 0);
  assert(roots.output.find("profiles cannot set allowed_roots") !=
         std::string::npos);

  const CommandResult url = delegate_call("badurl");
  assert(url.exit_code == 0);
  assert(url.output.find("profiles cannot set url_policy") !=
         std::string::npos);

  const CommandResult depth_key = delegate_call("baddepth");
  assert(depth_key.exit_code == 0);
  assert(depth_key.output.find("profiles cannot set delegation_max_depth") !=
         std::string::npos);

  Json self_args = Json::object();
  self_args["profile"] = "helper";
  self_args["task"] = "x";
  const CommandResult self = run_command(
      base + " --agent helper --call delegate --args " +
      quote(self_args.dump()));
  assert(self.exit_code == 0);
  assert(self.output.find("cannot delegate to itself") != std::string::npos);

  const CommandResult budget = delegate_call("greedy");
  assert(budget.exit_code == 0);
  assert(budget.output.find("delegation budget exhausted") !=
         std::string::npos);
}

// Plan mode removes write/edit/download/HTTP tools from the registry itself.
// A model that asks for fs_write gets a clean unknown-tool error and nothing is
// created; the tool schema sent to the provider has no mutating names.
void test_plan_mode_read_only() {
  const std::string directory = make_temp_dir();
  const std::string config_path = write_delegation_config(directory, 4, 500, 0);
  const std::string target = directory + "/plan-should-not-exist.txt";

  Json write_args = Json::object();
  write_args["path"] = target;
  write_args["content"] = "plan mode must not write this\n";
  StubProvider stub(
      {Reply{tool_call_reply("call_plan_write", "fs_write", write_args)},
       Reply{final_reply("plan complete")}});

  const CommandResult result = run_command(
      "./build/harness_e2e --config " + quote(config_path) +
      " --plan --base-url " + quote(stub.base_url()) +
      " --test-allow-loopback-cleartext " + quote("make a local plan"));
  if (result.exit_code != 0) {
    std::fprintf(stderr, "plan CLI exited %d:\n%s\n", result.exit_code,
                 result.output.c_str());
  }
  assert(result.exit_code == 0);
  assert(result.output.find("plan complete") != std::string::npos);
  assert(::access(target.c_str(), F_OK) != 0);
  assert(stub.wait_for_requests(2, 8000));
  assert(stub.request_count() == 2);

  const Json first = Json::parse(stub.request(0).body);
  assert(first["tools"].is_array());
  bool saw_fs_write = false;
  bool saw_fs_edit = false;
  bool saw_download = false;
  bool saw_http = false;
  bool saw_tree = false;
  bool saw_recon = false;
  bool saw_swarm = false;
  for (const Json& tool : first["tools"].items()) {
    const std::string name = tool["function"]["name"].as_string();
    if (name == "fs_write") saw_fs_write = true;
    if (name == "fs_edit") saw_fs_edit = true;
    if (name == "download") saw_download = true;
    if (name == "http_get") saw_http = true;
    if (name == "fs_tree") saw_tree = true;
    if (name == "env_info") saw_recon = true;
    if (name == "swarm") saw_swarm = true;
  }
  assert(!saw_fs_write);
  assert(!saw_fs_edit);
  assert(!saw_download);
  assert(!saw_http);
  assert(saw_tree);
  assert(saw_recon);
  assert(saw_swarm);
  assert(stub.request(1).body.find("unknown tool: fs_write") !=
         std::string::npos);

  const CommandResult tools = run_command(
      "./build/harness_e2e --config " + quote(config_path) + " --plan --tools");
  assert(tools.exit_code == 0);
  assert(tools.output.find("  fs_tree  (") != std::string::npos);
  assert(tools.output.find("  env_info  (") != std::string::npos);
  assert(tools.output.find("  swarm  (") != std::string::npos);
  assert(tools.output.find("  fs_write  (") == std::string::npos);
  assert(tools.output.find("  fs_edit  (") == std::string::npos);
  assert(tools.output.find("  download  (") == std::string::npos);
  assert(tools.output.find("  http_get  (") == std::string::npos);
}

// A mission runs sequential steps and a bounded swarm lane group through the
// real CLI. The second step must receive the first step's answer as context.
void test_mission_and_swarm() {
  const std::string directory = make_temp_dir();
  const std::string agents = directory + "/agents";
  assert(::mkdir(agents.c_str(), 0700) == 0);
  write_profile(agents, "scout",
                R"({"description":"Scout","system_prompt":"SCOUT PROMPT","max_steps":1,"max_tokens":100})");
  write_profile(agents, "mapper",
                R"({"description":"Mapper","system_prompt":"MAPPER PROMPT","max_steps":1,"max_tokens":100})");
  const std::string config_path = write_delegation_config(directory, 8, 2000, 2);
  const std::string mission_path = directory + "/mission.json";
  write_text(mission_path, R"({
    "name": "local-map",
    "steps": [
      {"id": "single", "profile": "scout", "task": "single lane",
       "max_steps": 1, "max_tokens": 100},
      {"id": "lanes", "swarm": [
        {"profile": "scout", "task": "lane one", "max_steps": 1, "max_tokens": 100},
        {"profile": "mapper", "task": "lane two", "max_steps": 1, "max_tokens": 100}
      ]}
    ]
  })");

  StubProvider stub({Reply{final_reply("single answer")},
                     Reply{final_reply("lane one answer")},
                     Reply{final_reply("lane two answer")}});
  const CommandResult result = run_command(
      "./build/harness_e2e --config " + quote(config_path) + " --agents " +
      quote(agents) + " --mission " + quote(mission_path) + " --base-url " +
      quote(stub.base_url()) + " --test-allow-loopback-cleartext");
  if (result.exit_code != 0) {
    std::fprintf(stderr, "mission CLI exited %d:\n%s\n", result.exit_code,
                 result.output.c_str());
  }
  assert(result.exit_code == 0);
  assert(result.output.find("local-map") != std::string::npos);
  assert(result.output.find("single answer") != std::string::npos);
  assert(result.output.find("lane one answer") != std::string::npos);
  assert(result.output.find("lane two answer") != std::string::npos);
  assert(result.output.find("\"width\": 2") != std::string::npos);
  assert(result.output.find("failed_step") == std::string::npos);
  assert(stub.wait_for_requests(3, 8000));
  assert(stub.request_count() == 3);

  const Recorded first = stub.request(0);
  assert(first.body.find("single lane") != std::string::npos);
  assert(first.body.find("Prior mission observations") == std::string::npos);
  for (std::size_t i = 1; i < 3; ++i) {
    const Recorded lane = stub.request(i);
    assert(lane.body.find("Prior mission observations") != std::string::npos);
    assert(lane.body.find("single answer") != std::string::npos);
    assert(lane.body.find("lane one") != std::string::npos ||
           lane.body.find("lane two") != std::string::npos);
  }
}

void test_swarm_width_limit() {
  const std::string directory = make_temp_dir();
  const std::string config_path = write_delegation_config(directory, 4, 500, 1);
  Json tasks = Json::array();
  for (int i = 0; i < 9; ++i) {
    Json task = Json::object();
    task["profile"] = "scout";
    task["task"] = "x";
    tasks.push_back(task);
  }
  Json arguments = Json::object();
  arguments["tasks"] = tasks;
  const CommandResult wide = run_command(
      "./build/harness_e2e --config " + quote(config_path) +
      " --call swarm --args " + quote(arguments.dump()));
  assert(wide.exit_code == 0);
  assert(wide.output.find("hard limit of 8") != std::string::npos);

  Json empty = Json::object();
  empty["tasks"] = Json::array();
  const CommandResult none = run_command(
      "./build/harness_e2e --config " + quote(config_path) +
      " --call swarm --args " + quote(empty.dump()));
  assert(none.exit_code == 0);
  assert(none.output.find("non-empty tasks array") != std::string::npos);
}

Agent make_agent(const StubProvider& stub, ToolRegistry& registry, LogFn log,
                 const std::string& key_env = std::string(),
                 RuntimeBudget budget = RuntimeBudget{}) {
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
               [](const std::string&) {}, UrlPolicy::test_only_loopback_policy(), SecretSet{},
               budget);
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

// Multiple tool calls in one turn run across the budgeted executor. Two calls
// overlap with two workers and serialize with one; each result still reaches
// the provider under the matching tool_call_id.
void test_parallel_tool_calls_follow_budget() {
  struct Counter {
    std::atomic<int> active{0};
    std::atomic<int> peak{0};
  };

  for (int workers : {2, 1}) {
    Counter counter;
    ToolRegistry registry;
    for (const char* name : {"wait_a", "wait_b"}) {
      Tool tool;
      tool.name = name;
      tool.description = "waits for the other call";
      tool.parameters = Json::object();
      tool.parameters["type"] = "object";
      tool.parameters["properties"] = Json::object();
      tool.timeout_ms = 5000;
      tool.handler = [&counter](const Json&) -> Json {
        const int now = counter.active.fetch_add(1) + 1;
        int previous = counter.peak.load();
        while (now > previous && !counter.peak.compare_exchange_weak(previous, now)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        counter.active.fetch_sub(1);
        return Json("waited");
      };
      registry.add(std::move(tool));
    }

    StubProvider stub({Reply{multi_tool_call_reply()}, Reply{final_reply("parallel done")}});
    RuntimeBudget budget;
    budget.workers = workers;
    Agent agent = make_agent(stub, registry, [](const std::string&) {}, std::string(), budget);
    assert(agent.workers() == workers);

    AgentOptions options;
    options.max_steps = 4;
    options.system_prompt = "test";
    assert(agent.run("go", options));
    assert(counter.peak.load() == workers);
    assert(stub.wait_for_requests(2, 5000));
    const Recorded second = stub.request(1);
    assert(second.body.find("\"tool_call_id\":\"call_a\"") != std::string::npos);
    assert(second.body.find("\"tool_call_id\":\"call_b\"") != std::string::npos);
    assert(second.body.find("waited") != std::string::npos);
  }
}

// Local model mode is narrow: it accepts a cleartext loopback /v1 endpoint
// without the test-only policy, never calls the key lookup, and never sends
// Authorization. Flipping it off restores the strict default policy.
void test_local_model_no_key() {
  StubProvider stub({Reply{final_reply("local answer")}});
  ToolRegistry registry;
  ModelConfig config;
  config.local_model = true;
  config.base_url = stub.base_url() + "/v1";
  config.model = "qwen3.5-0.8b";
  std::atomic<int> key_lookups{0};
  EnvFn env = [&key_lookups](const std::string&) -> std::string {
    ++key_lookups;
    return "must-not-be-used";
  };
  std::string log_text;
  LogFn log = [&log_text](const std::string& message) {
    log_text += message + "\n";
  };
  RuntimeBudget budget;
  budget.workers = 1;
  Agent agent(config, make_http_client(), registry, env, log, log,
              UrlPolicy{}, SecretSet{}, budget);
  AgentOptions options;
  options.max_steps = 2;
  options.system_prompt = "local test";

  const bool first_ok = agent.run("hello", options);
  if (!first_ok) {
    std::fprintf(stderr, "local model run failed:\n%s\n", log_text.c_str());
  }
  assert(first_ok);
  assert(key_lookups.load() == 0);
  assert(stub.wait_for_requests(1, 5000));
  const Recorded request = stub.request(0);
  assert(request.target.find("/v1/chat/completions") != std::string::npos);
  assert(request.headers.count("authorization") == 0);

  // The local flag is not a general cleartext/private-network bypass: turning
  // it off makes the same endpoint fail the ordinary production policy.
  config.local_model = false;
  agent.set_model_config(config);
  assert(!agent.run("hello again", options));
  assert(stub.request_count() == 1);
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
  std::printf("local agent profile...\n");
  test_local_agent_profile();
  std::printf("api key file...\n");
  test_api_key_file();
  std::printf("skill and edit tools...\n");
  test_skill_and_edit_tools();
  std::printf("delegation round trip...\n");
  test_delegation_round_trip();
  std::printf("delegation depth limit...\n");
  test_delegation_depth_limit();
  std::printf("delegation budget accounting...\n");
  test_delegation_budget_accounting();
  std::printf("delegation refusals...\n");
  test_delegation_refusals();
  std::printf("plan mode...\n");
  test_plan_mode_read_only();
  std::printf("mission and swarm...\n");
  test_mission_and_swarm();
  std::printf("swarm width...\n");
  test_swarm_width_limit();
  std::printf("malformed provider response...\n");
  test_malformed_provider_response();
  std::printf("unknown tool...\n");
  test_unknown_tool_is_a_clean_tool_error();
  std::printf("truncated response...\n");
  test_truncated_provider_response();
  std::printf("deadline...\n");
  test_deadline_returns_without_blocking();
  std::printf("parallel tool calls...\n");
  test_parallel_tool_calls_follow_budget();
  std::printf("local model no key...\n");
  test_local_model_no_key();
  std::printf("production binary...\n");
  test_production_binary_has_no_bypass();

  std::printf("e2e provider tests passed\n");
  return 0;
}
