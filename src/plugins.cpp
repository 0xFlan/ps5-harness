#include "ps5h/plugins.h"

#include <dirent.h>
#include <dlfcn.h>

#include <cstdlib>
#include <cstring>
#include <utility>

namespace ps5h {
namespace {

void host_log(void* user, const char* level, const char* message);
int host_register_tool(void* user, const char* name, const char* description,
                       const char* parameters_json, ps5h_tool_fn handler, void* tool_user);
const char* host_get_env(void* user, const char* name);
char* host_http_request(void* user, const char* request_json);
void host_free_string(void* user, char* value);

bool has_shared_library_suffix(const std::string& name) {
  if (name.size() > 3 && name.compare(name.size() - 3, 3, ".so") == 0) {
    return true;
  }
  if (name.size() > 6 && name.compare(name.size() - 6, 6, ".dylib") == 0) {
    return true;
  }
  return false;
}

}  // namespace

PluginHost::PluginHost(ToolRegistry& registry, HttpFn http, EnvFn env, LogFn log,
                       std::size_t max_body_bytes)
    : registry_(registry),
      http_(std::move(http)),
      env_(std::move(env)),
      log_(std::move(log)),
      max_body_bytes_(max_body_bytes) {}

PluginHost::~PluginHost() { unload_all(); }

void PluginHost::unload_all() {
  for (void* handle : handles_) {
    dlclose(handle);
  }
  handles_.clear();
}

const char* PluginHost::env_value_cstr(const std::string& name) {
  std::string value = env_ ? env_(name) : std::string();
  if (value.empty()) {
    const char* fallback = std::getenv(name.c_str());
    if (fallback != nullptr) {
      value = fallback;
    }
  }
  if (value.empty()) {
    return nullptr;
  }
  env_strings_.push_back(value);
  return env_strings_.back().c_str();
}

void PluginHost::log_message(const char* level, const char* message) {
  if (!log_ || message == nullptr) {
    return;
  }
  std::string prefix = level != nullptr ? level : "info";
  log_("plugin[" + prefix + "] " + message);
}

bool PluginHost::load(const std::string& path, std::string* error) {
  auto fail = [error](const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
  };

  void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    const char* message = dlerror();
    return fail(message != nullptr ? message : "dlopen failed");
  }

  using InitFn = int (*)(const ps5h_host_api*);
  auto init = reinterpret_cast<InitFn>(dlsym(handle, "ps5h_plugin_init"));
  if (init == nullptr) {
    dlclose(handle);
    return fail("plugin does not export ps5h_plugin_init");
  }

  ps5h_host_api host{};
  host.abi_version = PS5H_PLUGIN_ABI_VERSION;
  host.user = this;
  host.log = host_log;
  host.register_tool = host_register_tool;
  host.get_env = host_get_env;
  host.http_request = host_http_request;
  host.free_string = host_free_string;

  if (init(&host) != 0) {
    dlclose(handle);
    return fail("ps5h_plugin_init returned non-zero");
  }

  using IdFn = const char* (*)(void);
  std::string id = path;
  auto id_fn = reinterpret_cast<IdFn>(dlsym(handle, "ps5h_plugin_id"));
  if (id_fn != nullptr) {
    const char* reported = id_fn();
    if (reported != nullptr) {
      id = reported;
    }
  }

  handles_.push_back(handle);
  loaded_.push_back(id);
  if (log_) {
    log_("loaded plugin " + id);
  }
  return true;
}

std::size_t PluginHost::load_directory(const std::string& directory) {
  if (directory.empty()) {
    return 0;
  }
  DIR* dir = opendir(directory.c_str());
  if (dir == nullptr) {
    if (log_) log_("plugin directory not found: " + directory);
    return 0;
  }

  std::size_t count = 0;
  while (dirent* entry = readdir(dir)) {
    std::string name = entry->d_name;
    if (!has_shared_library_suffix(name)) {
      continue;
    }
    std::string error;
    if (load(directory + "/" + name, &error)) {
      ++count;
    } else if (log_) {
      log_("skipped plugin " + name + ": " + error);
    }
  }
  closedir(dir);
  return count;
}

namespace {

struct EmitState {
  Json value;
  bool emitted = false;
};

// Per-thread: the tool deadline may run a handler on a worker thread, and the
// plugin's emit callback only receives the plugin's own user pointer.
thread_local EmitState* g_emit_state = nullptr;

void plugin_emit(void* /*user*/, const char* json, std::size_t length) {
  EmitState* target = g_emit_state;
  if (target == nullptr || json == nullptr) {
    return;
  }
  std::string text(json, length);
  try {
    target->value = Json::parse(text);
  } catch (const JsonError&) {
    target->value = Json(text);
  }
  target->emitted = true;
}

void host_log(void* user, const char* level, const char* message) {
  auto* self = static_cast<PluginHost*>(user);
  if (self != nullptr) {
    self->log_message(level, message);
  }
}

int host_register_tool(void* user, const char* name, const char* description,
                       const char* parameters_json, ps5h_tool_fn handler, void* tool_user) {
  auto* self = static_cast<PluginHost*>(user);
  if (self == nullptr || name == nullptr || handler == nullptr) {
    return -1;
  }

  Json parameters = Json::object();
  parameters["type"] = "object";
  if (parameters_json != nullptr) {
    try {
      parameters = Json::parse(parameters_json);
    } catch (const JsonError&) {
      return -2;
    }
  }

  Tool tool;
  tool.name = name;
  tool.description = description != nullptr ? description : "";
  tool.parameters = parameters;
  tool.handler = [handler, tool_user](const Json& arguments) -> Json {
    EmitState state;
    std::string raw = arguments.dump();
    // The plugin writes into host memory; nothing is freed across the boundary.
    g_emit_state = &state;
    handler(tool_user, raw.c_str(), &plugin_emit);
    g_emit_state = nullptr;
    if (!state.emitted) {
      return Json("plugin produced no result");
    }
    return state.value;
  };

  if (!valid_tool_name(tool.name)) {
    return -3;
  }
  self->registry().add(std::move(tool));
  return 0;
}

const char* host_get_env(void* user, const char* name) {
  auto* self = static_cast<PluginHost*>(user);
  if (self == nullptr || name == nullptr) {
    return nullptr;
  }
  return self->env_value_cstr(name);
}

char* host_http_request(void* user, const char* request_json) {
  auto* self = static_cast<PluginHost*>(user);
  if (self == nullptr || request_json == nullptr) {
    return nullptr;
  }

  Json request;
  try {
    request = Json::parse(request_json);
  } catch (const JsonError&) {
    return nullptr;
  }

  HttpFn http = self->http();
  if (!http) {
    return nullptr;
  }

  HttpRequest outgoing;
  outgoing.max_body_bytes = self->max_body_bytes();
  outgoing.method = request["method"].as_string("GET");
  outgoing.url = request["url"].as_string();
  outgoing.body = request["body"].as_string();
  for (const auto& header : request["headers"].fields()) {
    outgoing.headers.emplace_back(header.first, header.second.as_string());
  }
  HttpResponse response = http(outgoing);

  Json out = Json::object();
  out["status"] = static_cast<long long>(response.status);
  out["body"] = response.body;
  out["error"] = response.error;
  std::string text = out.dump();
  char* buffer = static_cast<char*>(std::malloc(text.size() + 1));
  if (buffer == nullptr) {
    return nullptr;
  }
  std::memcpy(buffer, text.c_str(), text.size() + 1);
  return buffer;
}

void host_free_string(void* user, char* value) {
  (void)user;
  std::free(value);
}

}  // namespace
}  // namespace ps5h
