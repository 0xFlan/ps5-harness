#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "ps5h/json.h"

namespace ps5h {

using ToolHandler = std::function<Json(const Json& arguments)>;

struct Tool {
  std::string name;
  std::string description;
  Json parameters = Json::object();
  ToolHandler handler;
  int timeout_ms = 30000;
};

class ToolRegistry {
 public:
  void add(Tool tool);
  const Tool* find(const std::string& name) const;
  bool has(const std::string& name) const { return find(name) != nullptr; }
  const std::vector<Tool>& tools() const { return tools_; }

  // OpenAI-compatible "tools" array.
  Json schema_list() const;

 private:
  std::vector<Tool> tools_;
};

struct HttpRequest {
  std::string method = "GET";
  std::string url;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  // When set, the connection is pinned to this address (CURLOPT_RESOLVE), which
  // closes the DNS-rebinding window between the policy check and the request.
  std::string resolved_ip;
  std::size_t max_body_bytes = 4u * 1024u * 1024u;
  long timeout_seconds = 60;
  bool follow_redirects = false;
};

struct HttpResponse {
  long status = 0;
  std::string body;
  std::string error;

  bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

struct DownloadRequest {
  HttpRequest request;
  std::string path;
  std::string expected_sha256;
  unsigned long long max_bytes = 0;  // 0 = no cap
  bool resume = true;
};

struct DownloadResult {
  bool ok = false;
  long status = 0;
  unsigned long long bytes = 0;
  std::string sha256;
  std::string error;
};

using HttpFn = std::function<HttpResponse(const HttpRequest&)>;
using ProgressFn = std::function<void(unsigned long long received, unsigned long long total)>;
using CancelFn = std::function<bool()>;
using DownloadFn = std::function<DownloadResult(const DownloadRequest&, ProgressFn, CancelFn)>;
using LogFn = std::function<void(const std::string&)>;
using EnvFn = std::function<std::string(const std::string&)>;

// Tool names must satisfy the model API's function-name rules.
bool valid_tool_name(const std::string& name);
std::string sanitize_tool_name(const std::string& name);
std::string url_encode(const std::string& value);
std::string clip(const std::string& value, std::size_t limit);

// Builds the platform HTTP clients.
HttpFn make_http_client();
DownloadFn make_download_client();

// Runs a tool handler with a deadline. On timeout the handler's own thread is
// abandoned (it cannot be killed safely) and an error is returned; handlers copy
// their inputs, so the abandoned thread never touches caller state.
Json run_with_deadline(const ToolHandler& handler, const Json& arguments, int timeout_ms,
                       std::string* error);

}  // namespace ps5h
