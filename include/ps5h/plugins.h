#pragma once

#include <deque>
#include <string>
#include <vector>

#include "ps5h/core.h"
#include "ps5h/plugin_api.h"

namespace ps5h {

/*
 * Loads shared-object plugins (plugin_api.h) into the running harness.
 *
 * On the console a plugin is a .so/.prx built against this header with the same
 * SDK; on a host build it is a .dylib/.so. Loading is additive: a plugin that
 * fails to initialise is reported and skipped, never fatal.
 */
class PluginHost {
 public:
  PluginHost(ToolRegistry& registry, HttpFn http, EnvFn env, LogFn log,
             std::size_t max_body_bytes = 4u * 1024u * 1024u);
  ~PluginHost();

  PluginHost(const PluginHost&) = delete;
  PluginHost& operator=(const PluginHost&) = delete;

  bool load(const std::string& path, std::string* error);
  std::size_t load_directory(const std::string& directory);

  const std::vector<std::string>& loaded() const { return loaded_; }

  // The C ABI bridge uses these; they are not part of the caller-facing API.
  ToolRegistry& registry() { return registry_; }
  HttpFn http() const { return http_; }
  std::size_t max_body_bytes() const { return max_body_bytes_; }
  const char* env_value_cstr(const std::string& name);
  void log_message(const char* level, const char* message);

 private:
  void unload_all();

  ToolRegistry& registry_;
  HttpFn http_;
  EnvFn env_;
  LogFn log_;
  std::size_t max_body_bytes_ = 4u * 1024u * 1024u;
  std::vector<void*> handles_;
  std::vector<std::string> loaded_;
  // Backing storage for get_env(), which must stay valid for the process lifetime.
  std::deque<std::string> env_strings_;
};

}  // namespace ps5h
