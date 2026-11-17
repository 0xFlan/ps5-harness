#pragma once

#include <string>

#include "ps5h/core.h"
#include "ps5h/policy.h"

namespace ps5h {

/*
 * Turns a JSON manifest into registered tools, so a contributor can add an API
 * with no compiled code:
 *
 * {
 *   "id": "archive",
 *   "description": "Internet Archive catalogue",
 *   "baseUrl": "https://archive.org",
 *   "auth": { "type": "none" },
 *   "headers": { "Accept": "application/json" },
 *   "operations": {
 *     "search": {
 *       "method": "GET",
 *       "path": "/advancedsearch.php",
 *       "query": { "q": "{{query}}", "rows": "{{rows}}" },
 *       "pick": "response.docs",
 *       "description": "Search the catalogue"
 *     }
 *   }
 * }
 *
 * Each operation becomes tool "ep_<id>_<operation>". Placeholders in the path,
 * query values and body strings become string parameters of that tool.
 *
 * Every manifest URL is checked against UrlPolicy at load time and again at call
 * time, so a manifest cannot reach `file://`, cleartext, or the local network.
 */
class EndpointCompiler {
 public:
  EndpointCompiler(HttpFn http, EnvFn env, LogFn log, UrlPolicy policy = UrlPolicy{});

  bool load_manifest(const Json& manifest, ToolRegistry& registry, std::string* error);
  std::size_t load_directory(const std::string& directory, ToolRegistry& registry);

 private:
  HttpFn http_;
  EnvFn env_;
  LogFn log_;
  UrlPolicy policy_;
};

}  // namespace ps5h
