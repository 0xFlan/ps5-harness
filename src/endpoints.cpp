#include "ps5h/endpoints.h"

#include <dirent.h>

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ps5h {
namespace {

constexpr std::size_t kMaxResponseBytes = 20000;
constexpr std::size_t kMaxBufferedBytes = 4u * 1024u * 1024u;
constexpr int kDefaultToolTimeoutMs = 30000;

void collect_placeholders(const Json& node, std::set<std::string>& out) {
  if (node.is_string()) {
    const std::string& text = node.as_string();
    std::size_t position = 0;
    while ((position = text.find("{{", position)) != std::string::npos) {
      std::size_t end = text.find("}}", position + 2);
      if (end == std::string::npos) {
        break;
      }
      std::string name = text.substr(position + 2, end - position - 2);
      if (!name.empty() && valid_tool_name(name)) {
        out.insert(name);
      }
      position = end + 2;
    }
    return;
  }
  if (node.is_object()) {
    for (const auto& entry : node.fields()) {
      collect_placeholders(entry.second, out);
    }
    return;
  }
  if (node.is_array()) {
    for (const auto& item : node.items()) {
      collect_placeholders(item, out);
    }
  }
}

std::string substitute(const std::string& pattern, const Json& arguments,
                       std::string* missing) {
  std::string out;
  std::size_t position = 0;
  while (position < pattern.size()) {
    std::size_t start = pattern.find("{{", position);
    if (start == std::string::npos) {
      out.append(pattern, position, std::string::npos);
      break;
    }
    out.append(pattern, position, start - position);
    std::size_t end = pattern.find("}}", start + 2);
    if (end == std::string::npos) {
      out.append(pattern, start, std::string::npos);
      break;
    }
    std::string name = pattern.substr(start + 2, end - start - 2);
    const Json& value = arguments[name];
    std::string text;
    if (value.is_string()) {
      text = value.as_string();
    } else if (value.is_number()) {
      text = value.dump();
    } else if (value.is_bool()) {
      text = value.as_bool() ? "true" : "false";
    } else if (missing != nullptr && missing->empty()) {
      *missing = name;
    }
    out += text;
    position = end + 2;
  }
  return out;
}

std::string join_url(const std::string& base, const std::string& path) {
  if (path.empty()) {
    return base;
  }
  bool base_slash = !base.empty() && base.back() == '/';
  bool path_slash = path.front() == '/';
  if (base_slash && path_slash) {
    return base.substr(0, base.size() - 1) + path;
  }
  if (!base_slash && !path_slash) {
    return base + "/" + path;
  }
  return base + path;
}

}  // namespace

EndpointCompiler::EndpointCompiler(HttpFn http, EnvFn env, LogFn log, UrlPolicy policy)
    : http_(std::move(http)), env_(std::move(env)), log_(std::move(log)), policy_(std::move(policy)) {}

bool EndpointCompiler::load_manifest(const Json& manifest, ToolRegistry& registry,
                                     std::string* error) {
  auto fail = [error](const std::string& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };

  if (!manifest.is_object()) {
    return fail("manifest must be a JSON object");
  }
  const std::string id = sanitize_tool_name(manifest["id"].as_string());
  const std::string base_url = manifest["baseUrl"].as_string();
  if (id.empty()) {
    return fail("manifest is missing a usable \"id\"");
  }
  if (base_url.empty()) {
    return fail("manifest \"" + id + "\" is missing \"baseUrl\"");
  }
  const Json& operations = manifest["operations"];
  if (!operations.is_object() || operations.size() == 0) {
    return fail("manifest \"" + id + "\" has no operations");
  }

  // Checked here so a hostile manifest fails loudly at load, not mid-turn.
  auto base_decision = policy_.check(base_url);
  if (!base_decision.allowed) {
    return fail("manifest \"" + id + "\" baseUrl rejected: " + base_decision.reason);
  }

  const Json& auth = manifest["auth"];
  const std::string auth_type = auth["type"].as_string("none");
  const std::string auth_env = auth["env"].as_string();
  const std::string auth_header = auth["header"].as_string("Authorization");
  const std::string auth_param = auth["param"].as_string();

  HttpFn http = http_;
  EnvFn env = env_;
  const Json headers = manifest["headers"];
  const std::string manifest_description = manifest["description"].as_string();

  for (const auto& entry : operations.fields()) {
    const std::string operation_name = sanitize_tool_name(entry.first);
    const Json& operation = entry.second;
    if (operation_name.empty() || !operation.is_object()) {
      if (log_) {
        log_("skipping malformed operation in endpoint " + id);
      }
      continue;
    }

    std::set<std::string> placeholders;
    collect_placeholders(operation, placeholders);

    Json properties = Json::object();
    Json required = Json::array();
    for (const auto& name : placeholders) {
      Json property = Json::object();
      property["type"] = "string";
      property["description"] = "Value for {{" + name + "}}";
      properties[name] = property;
      required.push_back(name);
    }

    Json parameters = Json::object();
    parameters["type"] = "object";
    parameters["properties"] = properties;
    if (required.size() > 0) {
      parameters["required"] = required;
    }
    parameters["additionalProperties"] = false;

    const std::string method = operation["method"].as_string("GET");
    const std::string path = operation["path"].as_string();
    const Json query = operation["query"];
    const Json body = operation["body"];
    const std::string pick_path = operation["pick"].as_string();
    const std::string description =
        operation["description"].as_string(manifest_description + " — " + entry.first);
    const std::string tool_name = "ep_" + id + "_" + operation_name;

    Tool tool;
    tool.name = tool_name;
    tool.description = description;
    tool.parameters = parameters;
    const int operation_timeout = operation["timeout_seconds"].is_number()
                                      ? static_cast<int>(operation["timeout_seconds"].as_number() * 1000)
                                      : kDefaultToolTimeoutMs;
    tool.timeout_ms = operation_timeout;

    tool.handler = [http, env, method, path, base_url, query, body, pick_path, auth_type,
                    auth_env, auth_header, auth_param, headers,
                    policy = policy_](const Json& arguments) -> Json {
      if (!http) {
        return Json("no HTTP backend available in this build");
      }
      std::string missing;
      std::string url = join_url(base_url, substitute(path, arguments, &missing));
      if (!missing.empty()) {
        return Json("missing required argument: " + missing);
      }

      std::vector<std::string> pairs;
      for (const auto& query_entry : query.fields()) {
        std::string value = substitute(query_entry.second.as_string(), arguments, &missing);
        if (!missing.empty()) {
          return Json("missing required argument: " + missing);
        }
        pairs.push_back(url_encode(query_entry.first) + "=" + url_encode(value));
      }

      std::string body_text;
      if (!body.is_null()) {
        body_text = substitute(body.is_string() ? body.as_string() : body.dump(), arguments,
                               &missing);
        if (!missing.empty()) {
          return Json("missing required argument: " + missing);
        }
      }

      HttpRequest request;
      request.method = method;
      request.url = url;
      request.body = body_text;
      request.max_body_bytes = kMaxBufferedBytes;
      request.follow_redirects = false;

      for (const auto& header : headers.fields()) {
        request.headers.emplace_back(header.first, header.second.as_string());
      }
      if (auth_type == "bearer" && !auth_env.empty()) {
        std::string token = env ? env(auth_env) : std::string();
        if (token.empty()) {
          return Json("missing credential: environment variable " + auth_env + " is unset");
        }
        request.headers.emplace_back("Authorization", "Bearer " + token);
      } else if (auth_type == "header" && !auth_env.empty()) {
        std::string token = env ? env(auth_env) : std::string();
        if (token.empty()) {
          return Json("missing credential: environment variable " + auth_env + " is unset");
        }
        request.headers.emplace_back(auth_header, token);
      } else if (auth_type == "query" && !auth_env.empty() && !auth_param.empty()) {
        std::string token = env ? env(auth_env) : std::string();
        if (token.empty()) {
          return Json("missing credential: environment variable " + auth_env + " is unset");
        }
        pairs.push_back(url_encode(auth_param) + "=" + url_encode(token));
      }

      if (!pairs.empty()) {
        request.url += (request.url.find('?') == std::string::npos ? "?" : "&");
        for (std::size_t i = 0; i < pairs.size(); ++i) {
          if (i != 0) request.url += "&";
          request.url += pairs[i];
        }
      }

      // Re-check the final URL: arguments can change the path but never the host,
      // yet a malformed manifest could still smuggle one in.
      auto decision = policy.check(request.url);
      if (!decision.allowed) {
        return Json("blocked by URL policy: " + decision.reason);
      }
      request.resolved_ip = decision.resolved_ip;

      HttpResponse response = http(request);
      if (!response.error.empty()) {
        return Json("request failed: " + response.error);
      }

      Json result = Json::object();
      result["status"] = static_cast<long long>(response.status);

      Json parsed;
      bool parsed_ok = false;
      if (!response.body.empty()) {
        try {
          parsed = Json::parse(response.body);
          parsed_ok = true;
        } catch (const JsonError&) {
          parsed_ok = false;
        }
      }

      if (parsed_ok) {
        Json selected = pick_path.empty() ? parsed : parsed.pick(pick_path);
        result["result"] = selected;
      } else {
        result["body"] = clip(response.body, kMaxResponseBytes);
      }
      return result;
    };

    registry.add(std::move(tool));
  }
  return true;
}

std::size_t EndpointCompiler::load_directory(const std::string& directory,
                                             ToolRegistry& registry) {
  if (directory.empty()) {
    return 0;
  }
  DIR* dir = opendir(directory.c_str());
  if (dir == nullptr) {
    if (log_) {
      log_("endpoint directory not found: " + directory);
    }
    return 0;
  }

  std::size_t loaded = 0;
  while (dirent* entry = readdir(dir)) {
    std::string name = entry->d_name;
    if (name.size() < 6 || name.compare(name.size() - 5, 5, ".json") != 0) {
      continue;
    }
    std::string path = directory + "/" + name;
    std::string text;
    {
      FILE* file = fopen(path.c_str(), "rb");
      if (file == nullptr) {
        if (log_) log_("cannot open " + path);
        continue;
      }
      char buffer[8192];
      std::size_t read = 0;
      while ((read = fread(buffer, 1, sizeof buffer, file)) > 0) {
        text.append(buffer, read);
      }
      fclose(file);
    }

    std::string error;
    try {
      Json manifest = Json::parse(text);
      if (load_manifest(manifest, registry, &error)) {
        ++loaded;
      } else if (log_) {
        log_("skipped " + name + ": " + error);
      }
    } catch (const JsonError& failure) {
      if (log_) log_("skipped " + name + ": " + failure.what());
    }
  }
  closedir(dir);
  return loaded;
}

}  // namespace ps5h
