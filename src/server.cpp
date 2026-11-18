#include "ps5h/server.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
  std::atomic<std::size_t> context_chars{0};
  std::atomic<std::size_t> message_count{0};
  std::atomic<std::size_t> compactions{0};
  std::atomic<long long> prompt_tokens{0};
  std::atomic<long long> completion_tokens{0};
  std::atomic<long long> total_tokens{0};
  std::atomic<int> steps{0};
  std::atomic<bool> usage_reported{false};
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
            "script-src 'unsafe-inline'; img-src data:; connect-src 'self'\r\n";
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

std::string local_address() {
  // Prefer a real LAN address over loopback in the printed hint.
  std::string fallback = "127.0.0.1";
  FILE* file = std::fopen("/proc/net/fib_trie", "rb");
  if (file != nullptr) {
    std::fclose(file);
  }
  // getifaddrs is available on macOS and FreeBSD, including the PS5 payload.
  struct ifaddrs* addresses = nullptr;
  if (::getifaddrs(&addresses) != 0) return fallback;
  for (struct ifaddrs* it = addresses; it != nullptr; it = it->ifa_next) {
    if (it->ifa_addr == nullptr || it->ifa_addr->sa_family != AF_INET) continue;
    const auto* address = reinterpret_cast<const sockaddr_in*>(it->ifa_addr);
    char text[INET_ADDRSTRLEN] = {};
    if (::inet_ntop(AF_INET, &address->sin_addr, text, sizeof text) == nullptr) continue;
    const std::string value(text);
    if (value == "127.0.0.1") continue;
    fallback = value;
    if (value.rfind("192.168.", 0) == 0 || value.rfind("10.", 0) == 0) break;
  }
  ::freeifaddrs(addresses);
  return fallback;
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

Json state_json(const Shared& shared) {
  Json state = Json::object();
  state["build"] = shared.options.build_id;
  state["model"] = shared.options.model;
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
  return state;
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
    const ssize_t got = ::recv(fd, buffer, sizeof buffer, 0);
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

  if (path == "/api/state") {
    if (request.method != "GET") {
      respond_json(fd, 405, Json::object());
    } else {
      respond_json(fd, 200, state_json(*shared));
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
  refresh_metadata(shared.get());

  const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) {
    log("serve: cannot create socket");
    return 1;
  }
  int reuse = 1;
  ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(static_cast<unsigned short>(options.port));
  if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) {
    log("serve: cannot bind port " + std::to_string(options.port));
    ::close(listener);
    return 1;
  }
  if (::listen(listener, 16) != 0) {
    log("serve: cannot listen");
    ::close(listener);
    return 1;
  }
  sockaddr_in actual{};
  socklen_t actual_length = sizeof actual;
  int port = options.port;
  if (::getsockname(listener, reinterpret_cast<sockaddr*>(&actual), &actual_length) == 0) {
    port = ntohs(actual.sin_port);
  }
  const std::string ip = local_address();
  log("browser UI listening on 0.0.0.0:" + std::to_string(port) +
      (options.plan_mode ? " (plan/read-only mode)" : " (full toolset)"));
  std::fprintf(stderr,
               "\n  PS5 Harness UI: http://%s:%d/?token=%s\n"
               "  Token is the only credential; keep it off untrusted networks.\n\n",
               ip.c_str(), port, options.token.c_str());
  std::fflush(stderr);

  while (true) {
    sockaddr_in peer{};
    socklen_t peer_length = sizeof peer;
    const int fd = ::accept(listener, reinterpret_cast<sockaddr*>(&peer), &peer_length);
    if (fd < 0) {
      if (errno == EINTR) continue;
      log("serve: accept failed");
      break;
    }
    std::thread([fd, shared]() { handle_connection(fd, shared.get()); }).detach();
  }
  ::close(listener);
  return 0;
}

}  // namespace ps5h
