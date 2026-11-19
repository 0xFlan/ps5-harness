#include "ps5h/server.h"

#include <arpa/inet.h>

#include "ps5h/console_ui.h"
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ps5h/json.h"
#include "ps5h/web_ui.h"

namespace ps5h {
namespace {

constexpr std::size_t kMaxHeaderBytes = 32u * 1024u;
constexpr std::size_t kMaxBodyBytes = 1024u * 1024u;
constexpr std::size_t kMaxMessageChars = 32u * 1024u;

// OpenSSL is used only by the optional HTTPS listener. Each accepted TLS
// connection runs in its own thread, so the active SSL object is thread-local
// and the existing HTTP parsing/writing code can stay unchanged.
thread_local SSL* tls_connection = nullptr;

ssize_t receive_bytes(int fd, void* buffer, std::size_t length) {
  if (tls_connection == nullptr) return ::recv(fd, buffer, length, 0);
  const int got = SSL_read(tls_connection, buffer, static_cast<int>(length));
  if (got > 0) return got;
  const int error = SSL_get_error(tls_connection, got);
  if (error == SSL_ERROR_ZERO_RETURN) return 0;
  if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
    errno = EAGAIN;
  }
  return -1;
}

struct Request {
  std::string method;
  std::string target;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
};

struct Shared {
  ServerOptions options;
  Agent* agent = nullptr;
  ChatSession* session = nullptr;
  AgentOptions agent_options;
  const ToolRegistry* tools = nullptr;
  const ActivityLog* activity = nullptr;
  LogFn log;
  std::mutex chat;
  // /api/chat holds `chat` for a whole turn. This flag lets the console
  // surface report a running turn without trying to take that lock.
  std::atomic<bool> busy{false};
  // The browser may switch providers between turns. Keep a copy that /api/state
  // can read without waiting for a long provider turn, while the agent gets
  // the authoritative update under chat.
  mutable std::mutex model_mutex;
  ModelConfig model;
  std::atomic<std::size_t> context_chars{0};
  std::atomic<std::size_t> message_count{0};
  std::atomic<std::size_t> compactions{0};
  std::atomic<long long> prompt_tokens{0};
  std::atomic<long long> completion_tokens{0};
  std::atomic<long long> total_tokens{0};
  std::atomic<int> steps{0};
  std::atomic<bool> usage_reported{false};
  std::unique_ptr<HomeTile> home_tile;
  // Filled after the listeners have chosen their real ports. Discovery must
  // report the ports that actually opened, not the requested defaults. The
  // values change when a suspended console resumes and the sockets are rebound.
  std::atomic<int> http_port{0};
  std::atomic<int> https_port{0};
  std::atomic<unsigned long long> listener_generation{0};
  std::atomic<bool> shutdown_requested{false};
  long long started_at = 0;
};

std::string lower(std::string value) {
  for (char& c : value) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return value;
}

std::string trim(std::string value) {
  std::size_t begin = 0;
  while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t')) ++begin;
  std::size_t end = value.size();
  while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' ||
                         value[end - 1] == '\r' || value[end - 1] == '\n')) {
    --end;
  }
  return value.substr(begin, end - begin);
}

std::string header(const Request& request, const std::string& name) {
  const std::string wanted = lower(name);
  for (const auto& pair : request.headers) {
    if (lower(pair.first) == wanted) return pair.second;
  }
  return std::string();
}

bool parse_request(const std::string& raw, Request* request, std::string* error) {
  const std::size_t head_end = raw.find("\r\n\r\n");
  if (head_end == std::string::npos) {
    *error = "incomplete headers";
    return false;
  }
  const std::string head = raw.substr(0, head_end);
  std::size_t line_end = head.find("\r\n");
  const std::string request_line = head.substr(0, line_end);
  std::size_t first = request_line.find(' ');
  std::size_t second = first == std::string::npos ? std::string::npos
                                                  : request_line.find(' ', first + 1);
  if (first == std::string::npos || second == std::string::npos) {
    *error = "bad request line";
    return false;
  }
  request->method = request_line.substr(0, first);
  request->target = request_line.substr(first + 1, second - first - 1);
  std::size_t cursor = line_end == std::string::npos ? head.size() : line_end + 2;
  while (cursor < head.size()) {
    std::size_t end = head.find("\r\n", cursor);
    if (end == std::string::npos) end = head.size();
    const std::string line = head.substr(cursor, end - cursor);
    const std::size_t colon = line.find(':');
    if (colon != std::string::npos) {
      request->headers.emplace_back(trim(line.substr(0, colon)),
                                    trim(line.substr(colon + 1)));
    }
    cursor = end + 2;
  }
  const std::string length_text = header(*request, "content-length");
  if (!length_text.empty()) {
    char* end = nullptr;
    errno = 0;
    const unsigned long length = std::strtoul(length_text.c_str(), &end, 10);
    if (errno != 0 || end == length_text.c_str() || *end != '\0' ||
        length > kMaxBodyBytes) {
      *error = "invalid content-length";
      return false;
    }
    request->body = raw.substr(head_end + 4);
    if (request->body.size() < length) {
      *error = "short body";
      return false;
    }
    request->body.resize(length);
  } else {
    request->body = raw.substr(head_end + 4);
  }
  return true;
}

bool send_all(int fd, const std::string& data) {
  std::size_t sent = 0;
  while (sent < data.size()) {
    if (tls_connection != nullptr) {
      const int wrote = SSL_write(tls_connection, data.data() + sent,
                                  static_cast<int>(data.size() - sent));
      if (wrote <= 0) return false;
      sent += static_cast<std::size_t>(wrote);
      continue;
    }
#ifdef MSG_NOSIGNAL
    const int flags = MSG_NOSIGNAL;
#else
    const int flags = 0;
#endif
    const ssize_t wrote = ::send(fd, data.data() + sent, data.size() - sent, flags);
    if (wrote < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (wrote == 0) return false;
    sent += static_cast<std::size_t>(wrote);
  }
  return true;
}

std::string reason_phrase(int status) {
  switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    default: return "Error";
  }
}

void respond(int fd, int status, const std::string& type, const std::string& body) {
  std::string head = "HTTP/1.1 " + std::to_string(status) + " " + reason_phrase(status) +
                     "\r\nContent-Type: " + type +
                     "\r\nContent-Length: " + std::to_string(body.size()) +
                     "\r\nConnection: close\r\nCache-Control: no-store\r\n"
                     "X-Content-Type-Options: nosniff\r\n"
                     "X-Frame-Options: DENY\r\n"
                     "Referrer-Policy: no-referrer\r\n";
  if (type == "text/html; charset=utf-8") {
    head += "Content-Security-Policy: default-src 'self'; style-src 'unsafe-inline'; "
            "script-src 'unsafe-inline'; img-src 'self' data:; connect-src 'self'\r\n";
  }
  head += "\r\n";
  send_all(fd, head + body);
}

void respond_json(int fd, int status, const Json& value) {
  respond(fd, status, "application/json; charset=utf-8", value.dump());
}

bool authorized(const Request& request, const std::string& token) {
  if (token.empty()) return false;
  const std::string expected = "Bearer " + token;
  const std::string provided = header(request, "authorization");
  if (provided.size() != expected.size()) return false;
  unsigned char diff = 0;
  for (std::size_t i = 0; i < provided.size(); ++i) {
    diff |= static_cast<unsigned char>(provided[i] ^ expected[i]);
  }
  return diff == 0;
}

std::vector<std::string> local_addresses() {
  std::vector<std::string> addresses;
  struct ifaddrs* interfaces = nullptr;
  if (::getifaddrs(&interfaces) != 0) return addresses;
  for (struct ifaddrs* it = interfaces; it != nullptr; it = it->ifa_next) {
    if (it->ifa_addr == nullptr || it->ifa_addr->sa_family != AF_INET) continue;
    const auto* address = reinterpret_cast<const sockaddr_in*>(it->ifa_addr);
    char text[INET_ADDRSTRLEN] = {};
    if (::inet_ntop(AF_INET, &address->sin_addr, text, sizeof text) == nullptr) continue;
    const std::string value(text);
    if (value == "127.0.0.1") continue;
    if (std::find(addresses.begin(), addresses.end(), value) == addresses.end()) {
      addresses.push_back(value);
    }
  }
  ::freeifaddrs(interfaces);
  if (addresses.empty()) addresses.push_back("127.0.0.1");
  return addresses;
}

std::string local_address() {
  // Prefer a real LAN address over loopback in the printed hint.
  const std::vector<std::string> addresses = local_addresses();
  for (const std::string& value : addresses) {
    if (value.rfind("192.168.", 0) == 0 || value.rfind("10.", 0) == 0) {
      return value;
    }
  }
  return addresses.front();
}

// A listener can survive a PS5 rest cycle in a defunct state: the process is
// frozen, the system closes the underlying socket, and the fd still looks
// usable to the payload. The network signature is part of resume detection;
// an empty value means there is no non-loopback IPv4 interface yet.
std::string network_signature() {
#if defined(PS5H_TEST_HOOKS)
  // Host tests need to hold the network down without touching the machine's
  // interfaces. The gate is compiled only into test builds.
  const char* gate = std::getenv("PS5H_TEST_NETWORK_GATE");
  if (gate != nullptr && gate[0] != '\0' && ::access(gate, F_OK) == 0) {
    return "test-offline";
  }
#endif
  struct ifaddrs* interfaces = nullptr;
  if (::getifaddrs(&interfaces) != 0) return std::string();
  std::string signature;
  for (struct ifaddrs* it = interfaces; it != nullptr; it = it->ifa_next) {
    if (it->ifa_addr == nullptr || it->ifa_addr->sa_family != AF_INET) continue;
    if ((it->ifa_flags & IFF_UP) == 0 || (it->ifa_flags & IFF_LOOPBACK) != 0) continue;
    const auto* address = reinterpret_cast<const sockaddr_in*>(it->ifa_addr);
    char text[INET_ADDRSTRLEN] = {};
    if (::inet_ntop(AF_INET, &address->sin_addr, text, sizeof text) == nullptr) continue;
    if (!signature.empty()) signature.push_back(',');
    signature += text;
  }
  ::freeifaddrs(interfaces);
  return signature;
}

bool network_ready() {
#if defined(PS5H_TEST_HOOKS)
  const char* gate = std::getenv("PS5H_TEST_NETWORK_GATE");
  if (gate != nullptr && gate[0] != '\0' && ::access(gate, F_OK) == 0) {
    return false;
  }
#endif
  // Do not treat loopback as a usable LAN merely because it is up on a console
  // whose Ethernet/Wi-Fi interface is down. On a host with no non-loopback
  // interface at all, loopback is still enough to reopen for a local test.
  struct ifaddrs* interfaces = nullptr;
  if (::getifaddrs(&interfaces) != 0) return false;
  bool loopback = false;
  bool non_loopback = false;
  for (struct ifaddrs* it = interfaces; it != nullptr; it = it->ifa_next) {
    if (it->ifa_addr == nullptr || it->ifa_addr->sa_family != AF_INET) continue;
    if ((it->ifa_flags & IFF_LOOPBACK) != 0) {
      if ((it->ifa_flags & IFF_UP) != 0) loopback = true;
      continue;
    }
    non_loopback = true;
    if ((it->ifa_flags & IFF_UP) != 0) {
      ::freeifaddrs(interfaces);
      return true;
    }
  }
  ::freeifaddrs(interfaces);
  return !non_loopback && loopback;
}

// SIGCONT is delivered when the console resumes. Keep the handler limited to
// one async-signal-safe flag assignment; the listener loop consumes it on its
// next pass. A second resume after a failed rebind sets the flag again.
volatile sig_atomic_t g_resume_requested = 0;

extern "C" void on_resume_signal(int) {
  g_resume_requested = 1;
}

void install_resume_handler(LogFn log) {
  struct sigaction action {};
  action.sa_handler = on_resume_signal;
  sigemptyset(&action.sa_mask);
  // No SA_RESTART: poll() should return EINTR so the loop sees the resume
  // promptly instead of waiting for its full timeout.
  action.sa_flags = 0;
  if (::sigaction(SIGCONT, &action, nullptr) != 0) {
    log("serve: SIGCONT handler unavailable; using time and network checks");
  }
}

std::string one_line(const std::string& value, std::size_t limit) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    if (c == '\r' || c == '\n') out.push_back(' ');
    else out.push_back(c);
  }
  return clip(out, limit);
}

Json model_json(const ModelConfig& model) {
  Json out = Json::object();
  out["base_url"] = model.base_url;
  out["model"] = model.model;
  out["local"] = model.local_model;
  out["local_model_path"] = model.local_model_path;
  out["local_model_port"] = model.local_model_port;
  out["local_model_threads"] = model.local_model_threads;
  out["local_model_context"] = model.local_model_context;
  out["api_key_env"] = model.api_key_env;
  out["api_key_file"] = model.api_key_file;
  out["temperature"] = model.temperature;
  out["max_tokens"] = model.max_tokens;
  return out;
}

Json model_presets_json() {
  auto preset = [](const std::string& id, const std::string& label,
                   const std::string& base_url, const std::string& api_key_env,
                   const std::string& model,
                   const std::vector<std::string>& models, bool local) {
    Json item = Json::object();
    item["id"] = id;
    item["label"] = label;
    item["base_url"] = base_url;
    item["api_key_env"] = api_key_env;
    item["model"] = model;
    item["local"] = local;
    Json choices = Json::array();
    for (const std::string& choice : models) choices.push_back(choice);
    item["models"] = std::move(choices);
    return item;
  };
  Json out = Json::array();
  out.push_back(preset("local", "On-console (built-in)",
                       "http://127.0.0.1:18081/v1", "", "local",
                       {"local"}, true));
  out.push_back(preset("openai", "OpenAI", "https://api.openai.com/v1",
                       "OPENAI_API_KEY", "gpt-4o-mini",
                       {"gpt-4o-mini", "gpt-4o", "gpt-4.1-mini"}, false));
  out.push_back(preset("venice", "Venice", "https://api.venice.ai/api/v1",
                       "VENICE_API_KEY", "zai-org-glm-5-1",
                       {"zai-org-glm-5-1", "venice-uncensored-1-2",
                        "google-gemma-3-27b-it"}, false));
  out.push_back(preset("openrouter", "OpenRouter",
                       "https://openrouter.ai/api/v1", "OPENROUTER_API_KEY",
                       "openai/gpt-4o-mini",
                       {"openai/gpt-4o-mini", "anthropic/claude-3.5-sonnet",
                        "google/gemini-2.0-flash-001"}, false));
  out.push_back(preset("groq", "Groq", "https://api.groq.com/openai/v1",
                       "GROQ_API_KEY", "llama-3.3-70b-versatile",
                       {"llama-3.3-70b-versatile", "llama-3.1-8b-instant"}, false));
  return out;
}

bool field_has_controls(const std::string& value) {
  for (unsigned char c : value) {
    if (std::iscntrl(c) != 0) return true;
  }
  return false;
}

bool valid_env_name(const std::string& value) {
  if (value.empty()) return true;
  const unsigned char first = static_cast<unsigned char>(value[0]);
  if (std::isalpha(first) == 0 && first != '_') return false;
  for (unsigned char c : value) {
    if (std::isalnum(c) == 0 && c != '_') return false;
  }
  return true;
}

bool read_key_file(const std::string& path, std::string* key,
                   std::string* error) {
  key->clear();
  if (path.empty()) return true;
  if (path.size() > 4096) {
    *error = "api_key_file path is too long";
    return false;
  }
  FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    *error = "cannot read api_key_file";
    return false;
  }
  char buffer[8192];
  std::size_t total = 0;
  while (total < sizeof buffer) {
    const std::size_t got = std::fread(buffer + total, 1, sizeof buffer - total, file);
    if (got == 0) break;
    total += got;
  }
  const bool failed = std::ferror(file) != 0;
  const bool extra = std::fgetc(file) != EOF;
  std::fclose(file);
  if (failed || extra) {
    *error = "api_key_file must be 1..8192 bytes";
    return false;
  }
  while (total > 0 && (buffer[total - 1] == '\n' || buffer[total - 1] == '\r')) {
    --total;
  }
  if (total == 0) {
    *error = "api_key_file is empty";
    return false;
  }
  key->assign(buffer, total);
  return true;
}

bool apply_model_fields(const Json& input, ModelConfig* model,
                        std::string* key_override, bool* key_changed,
                        std::string* error) {
  if (input.contains("base_url")) {
    if (!input["base_url"].is_string()) {
      *error = "base_url must be a string";
      return false;
    }
    model->base_url = input["base_url"].as_string();
  }
  if (model->base_url.empty() || model->base_url.size() > 500 ||
      field_has_controls(model->base_url)) {
    *error = "base_url must be 1..500 characters without control characters";
    return false;
  }
  std::string check_url = model->base_url;
  if (!check_url.empty() && check_url.back() == '/') check_url.pop_back();
  if (check_url.empty()) {
    *error = "base_url must include a scheme and host";
    return false;
  }

  if (input.contains("model")) {
    if (!input["model"].is_string()) {
      *error = "model must be a string";
      return false;
    }
    model->model = input["model"].as_string();
  }
  if (model->model.empty() || model->model.size() > 200 ||
      field_has_controls(model->model)) {
    *error = "model must be 1..200 characters without control characters";
    return false;
  }
  if (input.contains("local")) {
    if (!input["local"].is_bool()) {
      *error = "local must be true or false";
      return false;
    }
    model->local_model = input["local"].as_bool();
  }
  if (input.contains("local_model_path")) {
    if (!input["local_model_path"].is_string()) {
      *error = "local_model_path must be a string";
      return false;
    }
    model->local_model_path = input["local_model_path"].as_string();
  }
  if (model->local_model_path.size() > 4096 ||
      field_has_controls(model->local_model_path)) {
    *error = "local_model_path must be at most 4096 characters";
    return false;
  }
  if (model->local_model && model->local_model_path.empty()) {
    *error = "local_model_path is required for the on-console model";
    return false;
  }
  if (input.contains("local_model_port")) {
    if (!input["local_model_port"].is_number()) {
      *error = "local_model_port must be a number";
      return false;
    }
    const double value = input["local_model_port"].as_number();
    if (!std::isfinite(value) || value < 1 || value > 65535 ||
        std::floor(value) != value) {
      *error = "local_model_port must be an integer between 1 and 65535";
      return false;
    }
    model->local_model_port = static_cast<int>(value);
  }
  if (input.contains("local_model_threads")) {
    if (!input["local_model_threads"].is_number()) {
      *error = "local_model_threads must be a number";
      return false;
    }
    const double value = input["local_model_threads"].as_number();
    if (!std::isfinite(value) || value < 1 || value > 8 ||
        std::floor(value) != value) {
      *error = "local_model_threads must be an integer between 1 and 8";
      return false;
    }
    model->local_model_threads = static_cast<int>(value);
  }
  if (input.contains("local_model_context")) {
    if (!input["local_model_context"].is_number()) {
      *error = "local_model_context must be a number";
      return false;
    }
    const double value = input["local_model_context"].as_number();
    if (!std::isfinite(value) || value < 512 || value > 65536 ||
        std::floor(value) != value) {
      *error = "local_model_context must be an integer between 512 and 65536";
      return false;
    }
    model->local_model_context = static_cast<int>(value);
  }
  if (input.contains("temperature")) {
    if (!input["temperature"].is_number()) {
      *error = "temperature must be a number";
      return false;
    }
    const double value = input["temperature"].as_number();
    if (!std::isfinite(value) || value < 0.0 || value > 2.0) {
      *error = "temperature must be between 0 and 2";
      return false;
    }
    model->temperature = value;
  }
  if (input.contains("max_tokens")) {
    if (!input["max_tokens"].is_number()) {
      *error = "max_tokens must be a number";
      return false;
    }
    const double value = input["max_tokens"].as_number();
    if (!std::isfinite(value) || value < 0.0 || value > 32768.0 ||
        std::floor(value) != value) {
      *error = "max_tokens must be an integer between 0 and 32768";
      return false;
    }
    model->max_tokens = static_cast<int>(value);
  }
  if (input.contains("api_key_env")) {
    if (!input["api_key_env"].is_string()) {
      *error = "api_key_env must be a string";
      return false;
    }
    model->api_key_env = input["api_key_env"].as_string();
  }
  if (!valid_env_name(model->api_key_env)) {
    *error = "api_key_env must be a valid environment variable name";
    return false;
  }
  if (input.contains("api_key_file")) {
    if (!input["api_key_file"].is_string()) {
      *error = "api_key_file must be a string";
      return false;
    }
    const std::string path = input["api_key_file"].as_string();
    if (path.size() > 4096 || field_has_controls(path)) {
      *error = "api_key_file must be a path without control characters";
      return false;
    }
    std::string new_key;
    std::string key_error;
    if (!read_key_file(path, &new_key, &key_error)) {
      *error = key_error;
      return false;
    }
    model->api_key_file = path;
    *key_override = std::move(new_key);
    *key_changed = true;
  }
  return true;
}

Json state_json(const Shared& shared) {
  if (shared.home_tile) shared.home_tile->refresh_if_due();
  Json state = Json::object();
  state["build"] = shared.options.build_id;
  ModelConfig model;
  {
    std::lock_guard<std::mutex> lock(shared.model_mutex);
    model = shared.model;
  }
  state["model"] = model.model;
  state["provider"] = model_json(model);
  state["mode"] = shared.options.plan_mode ? "plan" : "full";
  state["session_title"] = "Console workspace";
  Json tools = Json::array();
  for (const Tool& tool : shared.tools->tools()) {
    Json item = Json::object();
    item["name"] = tool.name;
    item["description"] = one_line(tool.description, 260);
    tools.push_back(item);
  }
  state["tools"] = tools;
  Json context = Json::object();
  context["chars"] = static_cast<long long>(shared.context_chars.load());
  context["limit"] = static_cast<long long>(shared.options.context_chars);
  context["messages"] = static_cast<long long>(shared.message_count.load());
  context["compactions"] = static_cast<long long>(shared.compactions.load());
  state["context"] = context;
  Json usage = Json::object();
  usage["prompt_tokens"] = shared.prompt_tokens.load();
  usage["completion_tokens"] = shared.completion_tokens.load();
  usage["total_tokens"] = shared.total_tokens.load();
  usage["steps"] = shared.steps.load();
  usage["reported"] = shared.usage_reported.load();
  state["usage"] = usage;
  state["events"] = shared.activity->snapshot();
  if (shared.home_tile) state["home_tile"] = shared.home_tile->state();
  return state;
}

// The agent's own screen, built from live state. Anything a running turn owns
// is skipped rather than waited on: a tool call must never block on the turn
// that is making it.
ConsoleScreen console_screen(Shared& shared) {
  ConsoleScreen view;
  view.build = shared.options.build_id;
  ModelConfig model;
  {
    std::lock_guard<std::mutex> lock(shared.model_mutex);
    model = shared.model;
  }
  view.model = model.model;
  std::string base = model.base_url;
  const std::size_t scheme = base.find("://");
  if (scheme != std::string::npos) base.erase(0, scheme + 3);
  const std::size_t slash = base.find('/');
  if (slash != std::string::npos) base.resize(slash);
  view.provider = model.local_model ? "on-console (loopback)" : base;
  view.mode = shared.options.plan_mode ? "plan" : "full";
  view.busy = shared.busy.load();
  {
    std::unique_lock<std::mutex> lock(shared.chat, std::try_to_lock);
    if (lock.owns_lock()) {
      const Json& messages = shared.session->messages;
      const std::size_t total = messages.size();
      const std::size_t begin = total > 8 ? total - 8 : 0;
      for (std::size_t i = begin; i < total; ++i) {
        const Json& message = messages[i];
        const std::string role = message["role"].as_string();
        if (role == "system") continue;
        view.transcript.push_back(role + ": " +
                                  one_line(message["content"].as_string(), 150));
      }
      if (view.transcript.empty()) {
        view.transcript.push_back("no conversation yet");
      }
    } else {
      view.transcript.push_back("a turn is running; the transcript is being written");
    }
  }
  const Json events = shared.activity->snapshot();
  for (const Json& event : events.items()) {
    view.activity.push_back(one_line(event.as_string(), 150));
  }
  for (const Tool& tool : shared.tools->tools()) {
    view.tools.emplace_back(tool.name, one_line(tool.description, 80));
  }
  if (shared.home_tile) view.home_tile = shared.home_tile->state();
  return view;
}

// Shared by POST /api/model and the console driver's Apply control so the two
// cannot drift: one chat lock, one set of field rules, one URL policy.
Json apply_model_request(Shared* shared, const Json& input, int* status) {
  *status = 200;
  auto refuse = [status](int code, const std::string& message) {
    *status = code;
    Json body = Json::object();
    body["ok"] = false;
    body["error"] = message;
    return body;
  };
  std::unique_lock<std::mutex> chat_lock(shared->chat, std::try_to_lock);
  if (!chat_lock.owns_lock()) {
    return refuse(409, "a turn is already running");
  }
  ModelConfig updated = shared->agent->model_config();
  std::string key_override;
  bool key_changed = false;
  std::string validation_error;
  if (!apply_model_fields(input, &updated, &key_override, &key_changed,
                          &validation_error)) {
    return refuse(400, validation_error);
  }
  std::string endpoint_check = updated.base_url;
  if (!endpoint_check.empty() && endpoint_check.back() == '/') {
    endpoint_check.pop_back();
  }
  if (updated.local_model) {
    std::string local_reason;
    if (!local_model_endpoint(endpoint_check + "/chat/completions", &local_reason)) {
      return refuse(400, "local model endpoint rejected: " + local_reason);
    }
  } else {
    const UrlPolicy::Decision endpoint =
        shared->options.policy.check(endpoint_check + "/chat/completions");
    if (!endpoint.allowed) {
      return refuse(400,
                    "provider endpoint rejected by URL policy: " + endpoint.reason);
    }
  }
  shared->agent->set_model_config(updated);
  if (key_changed) {
    shared->agent->set_api_key_override(key_override);
  }
  {
    std::lock_guard<std::mutex> lock(shared->model_mutex);
    shared->model = updated;
  }
  shared->options.model = updated.model;
  shared->log("model switch -> " + updated.model + " @ " + updated.base_url);
  Json body = Json::object();
  body["ok"] = true;
  body["current"] = model_json(updated);
  body["presets"] = model_presets_json();
  return body;
}

Json discovery_json(const Shared& shared) {
  Json out = Json::object();
  out["ok"] = true;
  out["service"] = "ps5-harness";
  out["protocol"] = 1;
  out["build"] = shared.options.build_id;
  out["title_id"] = shared.options.home_tile.title_id;
  out["http_port"] = shared.http_port.load();
  out["https_port"] = shared.https_port.load();
  out["started_at"] = shared.started_at;
  Json addresses = Json::array();
  Json urls = Json::array();
  for (const std::string& address : local_addresses()) {
    addresses.push_back(address);
    urls.push_back("http://" + address + ":" +
                   std::to_string(shared.http_port.load()) + "/");
    if (shared.https_port.load() > 0) {
      urls.push_back("https://" + address + ":" +
                     std::to_string(shared.https_port.load()) + "/");
    }
  }
  out["addresses"] = addresses;
  out["urls"] = urls;
  return out;
}

void refresh_metadata(Shared* shared) {
  shared->context_chars = shared->session->approximate_chars();
  shared->message_count = shared->session->message_count();
  shared->compactions = shared->session->compactions;
  shared->prompt_tokens = shared->session->last_stats.prompt_tokens;
  shared->completion_tokens = shared->session->last_stats.completion_tokens;
  shared->total_tokens = shared->session->last_stats.total_tokens;
  shared->steps = shared->session->last_stats.steps;
  shared->usage_reported = shared->session->last_stats.usage_reported;
}

void handle_connection(int fd, Shared* shared) {
  // A dropped browser must never take the harness down through SIGPIPE. The
  // platform client already sets this where available; keep the server local.
#ifdef SO_NOSIGPIPE
  int one = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
  std::string raw;
  char buffer[8192];
  bool complete = false;
  std::size_t needed = 0;
  while (raw.size() < kMaxHeaderBytes + kMaxBodyBytes) {
    const ssize_t got = receive_bytes(fd, buffer, sizeof buffer);
    if (got < 0) {
      if (errno == EINTR) continue;
      ::close(fd);
      return;
    }
    if (got == 0) break;
    raw.append(buffer, static_cast<std::size_t>(got));
    const std::size_t head = raw.find("\r\n\r\n");
    if (head != std::string::npos) {
      // Parse only enough to learn the declared body length; full validation
      // happens after the body is complete. Header names are case-insensitive.
      std::size_t body_length = 0;
      bool invalid_length = false;
      std::size_t cursor = 0;
      while (cursor < head) {
        std::size_t end = raw.find("\r\n", cursor);
        if (end == std::string::npos || end > head) end = head;
        const std::string line = raw.substr(cursor, end - cursor);
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos &&
            lower(trim(line.substr(0, colon))) == "content-length") {
          const std::string value = trim(line.substr(colon + 1));
          char* parsed = nullptr;
          errno = 0;
          const unsigned long length_value =
              std::strtoul(value.c_str(), &parsed, 10);
          if (errno != 0 || parsed == value.c_str() || *parsed != '\0' ||
              length_value > kMaxBodyBytes) {
            invalid_length = true;
          } else {
            body_length = length_value;
          }
        }
        cursor = end + 2;
      }
      needed = head + 4 + body_length;
      if (invalid_length) {
        respond(fd, 400, "text/plain; charset=utf-8", "invalid content-length\n");
        ::close(fd);
        return;
      }
      if (raw.size() >= needed) {
        complete = true;
        break;
      }
    }
  }
  if (!complete) {
    respond(fd, 413, "text/plain; charset=utf-8", "request too large or incomplete\n");
    ::close(fd);
    return;
  }

  Request request;
  std::string error;
  if (!parse_request(raw, &request, &error)) {
    respond(fd, 400, "text/plain; charset=utf-8", error + "\n");
    ::close(fd);
    return;
  }
  std::string path = request.target;
  const std::size_t query = path.find('?');
  if (query != std::string::npos) path.resize(query);

  if (path == "/" || path == "/index.html") {
    respond(fd, 200, "text/html; charset=utf-8", web_ui_html());
    ::close(fd);
    return;
  }
  if (path == "/favicon.ico") {
    respond(fd, 204, "image/x-icon", "");
    ::close(fd);
    return;
  }
  if (path == "/tile-icon.png") {
    // Tile art is public: it is the same generic icon written into the tile
    // folder, and the UI needs it in an <img> tag where bearer headers are not
    // available. It contains no session or token data.
    respond(fd, 200, "image/png", home_tile_icon_png());
    ::close(fd);
    return;
  }
  if (path == "/.well-known/ps5h") {
    // Public discovery document: identity and the real listener ports only.
    // It contains no token, session content, filesystem paths or tool data.
    if (request.method != "GET") {
      respond_json(fd, 405, Json::object());
    } else {
      respond_json(fd, 200, discovery_json(*shared));
    }
    ::close(fd);
    return;
  }
  if (path.rfind("/api/", 0) != 0) {
    respond(fd, 404, "text/plain; charset=utf-8", "not found\n");
    ::close(fd);
    return;
  }
  if (!authorized(request, shared->options.token)) {
    Json body = Json::object();
    body["ok"] = false;
    body["error"] = "unauthorized";
    respond_json(fd, 401, body);
    ::close(fd);
    return;
  }

  if (path == "/api/shutdown") {
    if (request.method != "POST") {
      respond_json(fd, 405, Json::object());
    } else {
      shared->shutdown_requested.store(true);
      Json body = Json::object();
      body["ok"] = true;
      respond_json(fd, 200, body);
    }
    ::close(fd);
    return;
  }

  // What the console driver sees, and what a browser needs to follow it. The
  // element list is not included: the page already knows its own controls.
  if (path == "/api/ui") {
    if (request.method != "GET") {
      respond_json(fd, 405, Json::object());
    } else if (!shared->options.ui) {
      Json body = Json::object();
      body["ok"] = false;
      body["error"] = "console surface unavailable";
      respond_json(fd, 503, body);
    } else {
      Json body = shared->options.ui->state();
      body["ok"] = true;
      respond_json(fd, 200, body);
    }
    ::close(fd);
    return;
  }

  // A person changing panel is part of the agent's perception: the next frame
  // shows the screen they are looking at. It also stops driving, so a browser is
  // not pulled back to a panel the agent opened and then left.
  if (path == "/api/ui/panel") {
    if (request.method != "POST") {
      respond_json(fd, 405, Json::object());
      ::close(fd);
      return;
    }
    Json input;
    try {
      input = Json::parse(request.body);
    } catch (const JsonError& failure) {
      Json body = Json::object();
      body["ok"] = false;
      body["error"] = std::string("invalid JSON: ") + failure.what();
      respond_json(fd, 400, body);
      ::close(fd);
      return;
    }
    const std::string panel = input["panel"].as_string();
    if (panel != "chat" && panel != "activity" && panel != "tools" &&
        panel != "model" && panel != "home") {
      Json body = Json::object();
      body["ok"] = false;
      body["error"] = "panel must be chat, activity, tools, model or home";
      respond_json(fd, 400, body);
      ::close(fd);
      return;
    }
    if (shared->options.ui) {
      shared->options.ui->set_panel(panel, false);
    }
    Json body = Json::object();
    body["ok"] = true;
    body["panel"] = shared->options.ui ? shared->options.ui->panel() : panel;
    respond_json(fd, 200, body);
    ::close(fd);
    return;
  }

  // The page's controller, keyboard and mouse move the same surface the agent
  // drives: a focused control is reported here, so the agent's next frame shows
  // where the person is. A person's focus ends the agent's follow window, so the
  // page is not pulled away while they are typing.
  if (path == "/api/ui/focus") {
    if (request.method != "POST") {
      respond_json(fd, 405, Json::object());
      ::close(fd);
      return;
    }
    if (!shared->options.ui) {
      Json body = Json::object();
      body["ok"] = false;
      body["error"] = "console surface unavailable";
      respond_json(fd, 503, body);
      ::close(fd);
      return;
    }
    Json input;
    try {
      input = Json::parse(request.body);
    } catch (const JsonError& failure) {
      Json body = Json::object();
      body["ok"] = false;
      body["error"] = std::string("invalid JSON: ") + failure.what();
      respond_json(fd, 400, body);
      ::close(fd);
      return;
    }
    const std::string focus = input["focus"].as_string();
    if (focus.empty()) {
      Json body = Json::object();
      body["ok"] = false;
      body["error"] = "focus must be a control id";
      respond_json(fd, 400, body);
      ::close(fd);
      return;
    }
    Json body = shared->options.ui->focus_element(focus);
    shared->options.ui->settle();
    respond_json(fd, body["ok"].as_bool(false) ? 200 : 400, body);
    ::close(fd);
    return;
  }

  if (path == "/api/state") {
    if (request.method != "GET") {
      respond_json(fd, 405, Json::object());
    } else {
      respond_json(fd, 200, state_json(*shared));
    }
    ::close(fd);
    return;
  }

  if (path == "/api/model") {
    if (request.method == "GET") {
      Json current;
      {
        std::lock_guard<std::mutex> lock(shared->model_mutex);
        current = model_json(shared->model);
      }
      Json body = Json::object();
      body["ok"] = true;
      body["current"] = current;
      body["presets"] = model_presets_json();
      respond_json(fd, 200, body);
      ::close(fd);
      return;
    }
    if (request.method != "POST") {
      respond_json(fd, 405, Json::object());
      ::close(fd);
      return;
    }
    Json input;
    try {
      input = Json::parse(request.body);
    } catch (const JsonError& failure) {
      Json body = Json::object();
      body["ok"] = false;
      body["error"] = std::string("invalid JSON: ") + failure.what();
      respond_json(fd, 400, body);
      ::close(fd);
      return;
    }
    if (!input.is_object()) {
      Json body = Json::object();
      body["ok"] = false;
      body["error"] = "model settings must be an object";
      respond_json(fd, 400, body);
      ::close(fd);
      return;
    }
    int status = 200;
    const Json body = apply_model_request(shared, input, &status);
    respond_json(fd, status, body);
    ::close(fd);
    return;
  }

  if (path == "/api/health") {
    if (request.method != "GET") {
      respond_json(fd, 405, Json::object());
    } else {
      Json body = Json::object();
      body["ok"] = true;
      body["build"] = shared->options.build_id;
      body["listener_generation"] = static_cast<long long>(
          shared->listener_generation.load());
      if (shared->home_tile) body["home_tile"] = shared->home_tile->state();
      respond_json(fd, 200, body);
    }
    ::close(fd);
    return;
  }

  if (path == "/api/home-tile") {
    if (!shared->home_tile) {
      Json body = Json::object();
      body["ok"] = false;
      body["error"] = "home tile manager unavailable";
      respond_json(fd, 503, body);
    } else if (request.method == "GET") {
      respond_json(fd, 200, shared->home_tile->check());
    } else if (request.method == "POST") {
      respond_json(fd, 200, shared->home_tile->install());
    } else if (request.method == "DELETE") {
      respond_json(fd, 200, shared->home_tile->remove());
    } else {
      respond_json(fd, 405, Json::object());
    }
    ::close(fd);
    return;
  }

  if (path == "/api/history") {
    if (request.method != "GET") {
      respond_json(fd, 405, Json::object());
    } else {
      std::lock_guard<std::mutex> lock(shared->chat);
      Json body = Json::object();
      body["messages"] = shared->session->messages;
      body["context_chars"] = static_cast<long long>(shared->session->context_chars);
      respond_json(fd, 200, body);
    }
    ::close(fd);
    return;
  }

  if (path == "/api/reset") {
    if (request.method != "POST") {
      respond_json(fd, 405, Json::object());
    } else {
      std::lock_guard<std::mutex> lock(shared->chat);
      shared->session->reset(shared->agent_options.system_prompt);
      std::string save_error;
      if (!shared->session->save(shared->options.session_path, &save_error)) {
        shared->log("session save failed: " + save_error);
      }
      refresh_metadata(shared);
      Json body = Json::object();
      body["ok"] = true;
      respond_json(fd, 200, body);
    }
    ::close(fd);
    return;
  }

  if (path == "/api/chat") {
    if (request.method != "POST") {
      respond_json(fd, 405, Json::object());
      ::close(fd);
      return;
    }
    std::unique_lock<std::mutex> lock(shared->chat, std::try_to_lock);
    if (!lock.owns_lock()) {
      Json body = Json::object();
      body["ok"] = false;
      body["error"] = "a turn is already running";
      respond_json(fd, 409, body);
      ::close(fd);
      return;
    }
    Json input;
    try {
      input = Json::parse(request.body);
    } catch (const JsonError& failure) {
      Json body = Json::object();
      body["ok"] = false;
      body["error"] = std::string("invalid JSON: ") + failure.what();
      respond_json(fd, 400, body);
      ::close(fd);
      return;
    }
    const std::string message = input["message"].as_string();
    if (message.empty() || message.size() > kMaxMessageChars) {
      Json body = Json::object();
      body["ok"] = false;
      body["error"] = "message must be 1..32768 bytes";
      respond_json(fd, 400, body);
      ::close(fd);
      return;
    }
    shared->busy.store(true);
    shared->log("ui user turn (" + std::to_string(message.size()) + " bytes)");
    std::string answer;
    std::string turn_error;
    const bool ok = shared->session->send(*shared->agent, message,
                                          shared->agent_options, &answer,
                                          &turn_error);
    std::string save_error;
    if (!shared->session->save(shared->options.session_path, &save_error)) {
      shared->log("session save failed: " + save_error);
    }
    refresh_metadata(shared);
    Json body = Json::object();
    body["ok"] = ok;
    body["answer"] = answer;
    body["error"] = turn_error;
    Json usage = Json::object();
    usage["prompt_tokens"] = shared->session->last_stats.prompt_tokens;
    usage["completion_tokens"] = shared->session->last_stats.completion_tokens;
    usage["total_tokens"] = shared->session->last_stats.total_tokens;
    usage["steps"] = shared->session->last_stats.steps;
    body["usage"] = usage;
    Json context = Json::object();
    context["chars"] = static_cast<long long>(shared->session->approximate_chars());
    context["limit"] = static_cast<long long>(shared->session->context_chars);
    context["messages"] = static_cast<long long>(shared->session->message_count());
    context["compactions"] = static_cast<long long>(shared->session->compactions);
    body["context"] = context;
    shared->busy.store(false);
    respond_json(fd, 200, body);
    ::close(fd);
    return;
  }

  respond(fd, 404, "text/plain; charset=utf-8", "not found\n");
  ::close(fd);
}

}  // namespace

void ActivityLog::add(const std::string& message) {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.push_back(message);
  while (entries_.size() > 200) entries_.pop_front();
}

Json ActivityLog::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  Json out = Json::array();
  const std::size_t begin = entries_.size() > 100 ? entries_.size() - 100 : 0;
  for (std::size_t i = begin; i < entries_.size(); ++i) {
    out.push_back(entries_[i]);
  }
  return out;
}

void ActivityLog::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.clear();
}

namespace {

SSL_CTX* make_tls_context(const ServerOptions& options, LogFn log) {
  if (options.tls_cert.empty() || options.tls_key.empty()) {
    log("serve: --serve-tls needs --tls-cert and --tls-key");
    return nullptr;
  }
  SSL_CTX* context = SSL_CTX_new(TLS_server_method());
  if (context == nullptr) {
    log("serve: cannot create TLS context");
    return nullptr;
  }
  SSL_CTX_set_options(context, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 |
                                   SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
  if (SSL_CTX_use_certificate_chain_file(context, options.tls_cert.c_str()) != 1) {
    log("serve: cannot load TLS certificate: " + options.tls_cert);
    SSL_CTX_free(context);
    return nullptr;
  }
  if (SSL_CTX_use_PrivateKey_file(context, options.tls_key.c_str(),
                                  SSL_FILETYPE_PEM) != 1) {
    log("serve: cannot load TLS private key: " + options.tls_key);
    SSL_CTX_free(context);
    return nullptr;
  }
  if (SSL_CTX_check_private_key(context) != 1) {
    log("serve: TLS certificate and key do not match");
    SSL_CTX_free(context);
    return nullptr;
  }
  return context;
}

int open_listener(int requested_port, int* actual_port, std::string* error) {
  const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) {
    *error = "cannot create socket";
    return -1;
  }
  int reuse = 1;
  ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(static_cast<unsigned short>(requested_port));
  if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) {
    *error = "cannot bind port " + std::to_string(requested_port);
    ::close(listener);
    return -1;
  }
  if (::listen(listener, 16) != 0) {
    *error = "cannot listen";
    ::close(listener);
    return -1;
  }
  sockaddr_in actual{};
  socklen_t actual_length = sizeof actual;
  *actual_port = requested_port;
  if (::getsockname(listener, reinterpret_cast<sockaddr*>(&actual), &actual_length) == 0) {
    *actual_port = ntohs(actual.sin_port);
  }
  return listener;
}

int open_discovery_socket(int port, std::string* error) {
  const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    *error = "cannot create discovery socket";
    return -1;
  }
  int reuse = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(static_cast<unsigned short>(port));
  if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) {
    *error = "cannot bind discovery port " + std::to_string(port);
    ::close(fd);
    return -1;
  }
  return fd;
}

bool serve_discovery_once(int fd, Shared* shared) {
  char buffer[128];
  sockaddr_in peer{};
  socklen_t peer_length = sizeof peer;
  const ssize_t got = ::recvfrom(fd, buffer, sizeof buffer - 1, 0,
                                 reinterpret_cast<sockaddr*>(&peer),
                                 &peer_length);
  if (got < 0) {
    if (errno == EINTR || errno == EAGAIN) return true;
    shared->log("discovery: receive failed");
    return false;
  }
  const std::string query = trim(std::string(buffer, static_cast<std::size_t>(got)));
  if (query != "PS5H_DISCOVER_V1") return true;
  const std::string answer = discovery_json(*shared).dump();
  ::sendto(fd, answer.data(), answer.size(), 0,
           reinterpret_cast<sockaddr*>(&peer), peer_length);
  return true;
}

bool accept_http(int listener, Shared* shared) {
  sockaddr_in peer{};
  socklen_t peer_length = sizeof peer;
  const int fd = ::accept(listener, reinterpret_cast<sockaddr*>(&peer), &peer_length);
  if (fd < 0) {
    if (errno == EINTR || errno == EAGAIN || errno == ECONNABORTED) return true;
    shared->log("serve: accept failed");
    return false;
  }
  std::thread([fd, shared]() { handle_connection(fd, shared); }).detach();
  return true;
}

bool accept_tls(int listener, SSL_CTX* tls_context, Shared* shared) {
  sockaddr_in peer{};
  socklen_t peer_length = sizeof peer;
  const int fd = ::accept(listener, reinterpret_cast<sockaddr*>(&peer), &peer_length);
  if (fd < 0) {
    if (errno == EINTR || errno == EAGAIN || errno == ECONNABORTED) return true;
    shared->log("serve: accept failed");
    return false;
  }
  SSL* ssl = SSL_new(tls_context);
  if (ssl == nullptr || SSL_set_fd(ssl, fd) != 1) {
    if (ssl != nullptr) SSL_free(ssl);
    ::close(fd);
    shared->log("serve: cannot create TLS connection");
    return true;
  }
  std::thread([fd, ssl, shared]() {
    if (SSL_accept(ssl) != 1) {
      shared->log("serve: TLS handshake failed");
      SSL_free(ssl);
      ::close(fd);
      return;
    }
    tls_connection = ssl;
    struct TlsGuard {
      SSL* ssl;
      ~TlsGuard() {
        tls_connection = nullptr;
        SSL_free(ssl);
      }
    } guard{ssl};
    handle_connection(fd, shared);
  }).detach();
  return true;
}

}  // namespace

int run_server(const ServerOptions& options, Agent& agent, ChatSession& session,
               const AgentOptions& agent_options, const ToolRegistry& tools,
               const ActivityLog& activity, LogFn log) {
  auto shared = std::make_shared<Shared>();
  shared->options = options;
  shared->agent = &agent;
  shared->session = &session;
  shared->agent_options = agent_options;
  shared->tools = &tools;
  shared->activity = &activity;
  shared->log = log;
  shared->model = agent.model_config();
  refresh_metadata(shared.get());

  // Install this before the first listener opens. A browser can connect as
  // soon as accept() is possible, and a resume signal must not be missed while
  // the rest of the server is still starting up.
  install_resume_handler(log);

  int port = options.port;
  std::string bind_error;
  int listener = open_listener(options.port, &port, &bind_error);
  if (listener < 0) {
    log("serve: " + bind_error);
    return 1;
  }

  SSL_CTX* tls_context = nullptr;
  int https_port = 0;
  int tls_listener = -1;
  if (options.https || options.https_port > 0) {
    tls_context = make_tls_context(options, log);
    if (tls_context == nullptr) {
      ::close(listener);
      return 1;
    }
    tls_listener = open_listener(options.https_port, &https_port, &bind_error);
    if (tls_listener < 0) {
      log("serve: HTTPS " + bind_error);
      SSL_CTX_free(tls_context);
      ::close(listener);
      return 1;
    }
  }

  // The home tile is created after the listeners so a changed --serve port is
  // written into the tile instead of the requested/default value. Install it
  // before accepting browser requests so the first state poll already has a
  // real registration answer. A missing ShadowMount service or web-app shell
  // is recorded as Pending, not fatal.
  shared->http_port = port;
  shared->https_port = https_port;
  shared->listener_generation = 1;
  shared->started_at = static_cast<long long>(std::time(nullptr));
  shared->options.home_tile.deeplink_port = port;
  shared->home_tile = std::make_unique<HomeTile>(shared->options.home_tile,
                                                 shared->options.http, log);
  shared->home_tile->start();

  // Bind the agent's screen to this process. The view reads live state and the
  // actions reuse the same code paths as the browser API, so a press cannot do
  // anything the page could not do. The callback captures a raw pointer: the
  // server outlives every turn and the surface is owned by `options`.
  if (shared->options.ui) {
    Shared* raw = shared.get();
    shared->options.ui->set_view([raw]() { return console_screen(*raw); });
    shared->options.ui->set_activate(
        [raw](const std::string& id, const Json& value) -> Json {
          auto error = [](const std::string& message) {
            Json out = Json::object();
            out["ok"] = false;
            out["error"] = message;
            return out;
          };
          if (id == "chat:send") {
            std::unique_lock<std::mutex> lock(raw->chat, std::try_to_lock);
            if (!lock.owns_lock()) {
              return error("a turn is already running; the console cannot start a second one");
            }
            const std::string message = value["message"].as_string();
            if (message.empty()) return error("message is empty");
            raw->busy.store(true);
            raw->log("ui console turn (" + std::to_string(message.size()) +
                     " bytes)");
            std::string answer;
            std::string turn_error;
            const bool ok = raw->session->send(*raw->agent, message,
                                               raw->agent_options, &answer,
                                               &turn_error);
            std::string save_error;
            if (!raw->session->save(raw->options.session_path, &save_error)) {
              raw->log("session save failed: " + save_error);
            }
            refresh_metadata(raw);
            raw->busy.store(false);
            Json out = Json::object();
            out["ok"] = ok;
            out["answer"] = answer;
            out["error"] = turn_error;
            return out;
          }
          if (id == "session:reset") {
            std::unique_lock<std::mutex> lock(raw->chat, std::try_to_lock);
            if (!lock.owns_lock()) return error("a turn is already running");
            raw->session->reset(raw->agent_options.system_prompt);
            std::string save_error;
            if (!raw->session->save(raw->options.session_path, &save_error)) {
              raw->log("session save failed: " + save_error);
            }
            refresh_metadata(raw);
            Json out = Json::object();
            out["ok"] = true;
            return out;
          }
          if (id == "model:apply") {
            Json input = value;
            if (input["local"].as_bool(false) &&
                !input.contains("local_model_path")) {
              input["local_model_path"] =
                  raw->agent->model_config().local_model_path;
            }
            int status = 200;
            return apply_model_request(raw, input, &status);
          }
          if (id == "home:add" || id == "home:check" || id == "home:remove") {
            if (!raw->home_tile) return error("home tile manager unavailable");
            if (id == "home:add") return raw->home_tile->install();
            if (id == "home:remove") return raw->home_tile->remove();
            return raw->home_tile->check();
          }
          return error("the console surface does not know how to '" + id + "'");
        });
  }

  const std::string ip = local_address();
  log("browser UI listening on 0.0.0.0:" + std::to_string(port) +
      (options.plan_mode ? " (plan/read-only mode)" : " (full toolset)"));
  if (tls_listener >= 0) {
    log("browser UI TLS listening on 0.0.0.0:" + std::to_string(https_port));
  }
  std::fprintf(stderr,
               "\n  PS5 Harness UI: http://%s:%d/?token=%s\n"
               "  Token is the only credential; keep it off untrusted networks.\n\n",
               ip.c_str(), port, options.token.c_str());
  if (tls_listener >= 0) {
    std::fprintf(stderr, "  PS5 Harness UI (HTTPS): https://%s:%d/?token=%s\n\n",
                 ip.c_str(), https_port, options.token.c_str());
  }
  std::fflush(stderr);

  int discovery_fd = -1;
  const bool discovery_enabled = options.discovery_port > 0;
  if (discovery_enabled) {
    std::string discovery_error;
    discovery_fd = open_discovery_socket(options.discovery_port, &discovery_error);
    if (discovery_fd < 0) {
      log("discovery: " + discovery_error + "; UDP discovery is unavailable");
    } else {
      log("discovery: UDP " + std::to_string(options.discovery_port) +
          " (query PS5H_DISCOVER_V1)");
    }
  }

  // A PS5 rest cycle can freeze this process and close its listening sockets
  // underneath it. Firmware that reports resume sends SIGCONT; the wall clock
  // can jump by five seconds or more, and the non-loopback IPv4 address set
  // may disappear or change. Watch all three because no single signal is
  // guaranteed on every firmware. Keep the old listeners in the poll set while
  // waiting: if they are still usable they must keep serving, and a wake with
  // no network must not turn the loop into a spin or a blocked sleep.
  std::time_t last_wall_clock = std::time(nullptr);
  std::string last_network = network_signature();
  bool rebind_pending = false;
  int rebind_backoff_seconds = 2;
  std::chrono::steady_clock::time_point next_rebind_attempt =
      std::chrono::steady_clock::now();

  auto close_listener = [](int* fd) {
    if (*fd < 0) return;
    // shutdown() wakes a thread that might still be waiting on this socket;
    // the listener loop itself is poll-driven, but accepted TLS handshakes can
    // be blocked on their own fds when rest mode begins.
    ::shutdown(*fd, SHUT_RDWR);
    ::close(*fd);
    *fd = -1;
  };

  auto begin_rebind = [&](const std::string& reason, bool immediate) {
    const auto now = std::chrono::steady_clock::now();
    if (!rebind_pending) {
      shared->log("serve: " + reason +
                  "; keeping listeners while the network recovers");
      rebind_pending = true;
    }
    rebind_backoff_seconds = 2;
    // A first wake gets a short settling period. A second SIGCONT or a network
    // transition retries immediately so a previous failed attempt cannot pin
    // the server to its backoff forever.
    next_rebind_attempt = immediate ? now : now + std::chrono::seconds(2);
  };

  auto rebind_listeners = [&]() -> bool {
    if (!network_ready()) return false;

    // The old descriptors are closed only at the point of a real attempt. A
    // still-valid listener serves requests during the network wait above.
    close_listener(&listener);
    close_listener(&tls_listener);
    close_listener(&discovery_fd);

    int new_port = port;
    std::string error;
    int new_listener = open_listener(port, &new_port, &error);
    if (new_listener < 0) {
      shared->log("serve: resume rebind failed: " + error);
      return false;
    }

    int new_https_port = https_port;
    int new_tls_listener = -1;
    if (tls_context != nullptr) {
      new_tls_listener = open_listener(https_port, &new_https_port, &error);
      if (new_tls_listener < 0) {
        shared->log("serve: HTTPS resume rebind failed: " + error);
        close_listener(&new_listener);
        return false;
      }
    }

    listener = new_listener;
    port = new_port;
    tls_listener = new_tls_listener;
    https_port = new_https_port;
    shared->http_port = port;
    shared->https_port = https_port;

    if (discovery_enabled) {
      std::string discovery_error;
      discovery_fd = open_discovery_socket(options.discovery_port, &discovery_error);
      if (discovery_fd < 0) {
        shared->log("discovery: " + discovery_error +
                    "; UDP discovery will retry after the next resume");
      }
    }

    const unsigned long long generation =
        shared->listener_generation.fetch_add(1) + 1;
    shared->log("serve: listeners reopened after resume (generation " +
                std::to_string(generation) + ")");
    rebind_pending = false;
    rebind_backoff_seconds = 2;
    return true;
  };

  while (true) {
    if (shared->shutdown_requested.load()) {
      shared->log("serve: shutdown requested");
      break;
    }
    const auto loop_now = std::chrono::steady_clock::now();
    const std::time_t wall_clock = std::time(nullptr);
    const bool time_jumped = wall_clock - last_wall_clock >= 5;
    last_wall_clock = wall_clock;
    const std::string network = network_signature();
    const bool network_changed = network != last_network;
    last_network = network;
    bool resume_signal = false;
    if (g_resume_requested != 0) {
      g_resume_requested = 0;
      resume_signal = true;
    }

    if (resume_signal || time_jumped || network_changed) {
      begin_rebind("suspend/resume detected",
                   network_changed || rebind_pending);
    }

    if (rebind_pending && loop_now >= next_rebind_attempt) {
      if (!network_ready()) {
        // The network is still down. Keep the old listeners in the poll set
        // and check again in one second; this is bounded and does not block.
        next_rebind_attempt = loop_now + std::chrono::seconds(1);
      } else if (!rebind_listeners()) {
        const int retry_seconds = rebind_backoff_seconds;
        shared->log("serve: rebind retry in " + std::to_string(retry_seconds) +
                    "s");
        next_rebind_attempt = loop_now + std::chrono::seconds(retry_seconds);
        rebind_backoff_seconds = std::min(30, rebind_backoff_seconds * 2);
      }
    }

    pollfd descriptors[3] = {};
    int descriptor_count = 0;
    int http_index = -1;
    int tls_index = -1;
    int discovery_index = -1;
    auto watch = [&](int fd) {
      if (fd < 0) return -1;
      descriptors[descriptor_count].fd = fd;
      descriptors[descriptor_count].events = POLLIN;
      return descriptor_count++;
    };
    http_index = watch(listener);
    tls_index = watch(tls_listener);
    discovery_index = watch(discovery_fd);

    int timeout_ms = 1000;
    if (rebind_pending) {
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          next_rebind_attempt - loop_now).count();
      timeout_ms = static_cast<int>(
          std::max<long long>(25, std::min<long long>(1000, remaining)));
    }

    const int ready = ::poll(descriptors, static_cast<nfds_t>(descriptor_count), timeout_ms);
    if (ready < 0) {
      if (errno == EINTR) continue;
      shared->log("serve: poll failed; retrying");
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }
    if (ready == 0) continue;

    if (http_index >= 0 && descriptors[http_index].revents != 0) {
      const short events = descriptors[http_index].revents;
      if ((events & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        close_listener(&listener);
        begin_rebind("HTTP listener lost", true);
      } else if ((events & POLLIN) != 0 && !accept_http(listener, shared.get())) {
        close_listener(&listener);
        begin_rebind("HTTP listener lost", true);
      }
    }
    if (tls_index >= 0 && descriptors[tls_index].revents != 0) {
      const short events = descriptors[tls_index].revents;
      if ((events & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        close_listener(&tls_listener);
        begin_rebind("HTTPS listener lost", true);
      } else if ((events & POLLIN) != 0 &&
                 !accept_tls(tls_listener, tls_context, shared.get())) {
        close_listener(&tls_listener);
        begin_rebind("HTTPS listener lost", true);
      }
    }
    if (discovery_index >= 0 && descriptors[discovery_index].revents != 0) {
      const short events = descriptors[discovery_index].revents;
      if ((events & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        close_listener(&discovery_fd);
        begin_rebind("discovery listener lost", true);
      } else if ((events & POLLIN) != 0 &&
                 !serve_discovery_once(discovery_fd, shared.get())) {
        close_listener(&discovery_fd);
        begin_rebind("discovery listener lost", true);
      }
    }
  }

  close_listener(&listener);
  close_listener(&tls_listener);
  close_listener(&discovery_fd);
  if (tls_context != nullptr) {
    SSL_CTX_free(tls_context);
  }
  return 0;
}

}  // namespace ps5h
