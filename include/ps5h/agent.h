#pragma once

#include <string>

#include "ps5h/core.h"
#include "ps5h/policy.h"
#include "ps5h/pool.h"
#include "ps5h/runtime.h"
#include "ps5h/secrets.h"

namespace ps5h {

struct ModelConfig {
  std::string base_url = "https://api.openai.com/v1";
  std::string model = "gpt-4o-mini";
  // The local model mode starts llama.cpp inside this process. It is
  // loopback-only, cleartext-only and sends no Authorization header. The
  // default remains a remote HTTPS provider; this is an explicit opt-in.
  bool local_model = false;
  std::string local_model_path;
  int local_model_port = 18081;
  int local_model_threads = 4;
  int local_model_context = 4096;
  std::string api_key_env = "PS5H_API_KEY";
  // Optional console-friendly fallback for a loader that cannot set env vars.
  // The key itself is never stored in the config; this is only a path.
  std::string api_key_file;
  double temperature = 0.2;
  int max_tokens = 0;
};

struct AgentOptions {
  int max_steps = 12;
  std::string system_prompt;
};

// Usage for one agent run. A delegator charges the child's own steps/tokens
// through its shared budget; this struct does not include grandchildren.
struct AgentRunStats {
  int steps = 0;
  long long prompt_tokens = 0;
  long long completion_tokens = 0;
  long long total_tokens = 0;
  bool usage_reported = false;
  bool usage_complete = true;  // every completion in this run reported usage
};

class Agent {
 public:
  Agent(ModelConfig config, HttpFn http, ToolRegistry& registry, EnvFn env, LogFn log,
        LogFn say, UrlPolicy policy, SecretSet secrets,
        RuntimeBudget budget = RuntimeBudget{});

  bool run(const std::string& prompt, const AgentOptions& options,
           AgentRunStats* stats = nullptr);

  // Continues an existing OpenAI-style message array. The caller owns the
  // system message and prior turns; this appends the assistant/tool messages
  // for the new turn. `answer` receives the final assistant text.
  bool run_messages(Json* messages, const AgentOptions& options,
                    AgentRunStats* stats = nullptr,
                    std::string* answer = nullptr);

  // Compresses a transcript into a bounded summary for context rollover.
  // Uses no tools and no recursive agent run.
  std::string summarize(const std::string& transcript);
  int workers() const { return executor_.workers(); }

  // The browser server changes the provider between turns. These accessors are
  // not independently synchronized; callers must serialize an update with
  // run/run_messages (the server holds its chat mutex for both).
  ModelConfig model_config() const { return config_; }
  void set_model_config(const ModelConfig& config) { config_ = config; }
  void set_api_key_override(const std::string& key);

 private:
  Json completion(const Json& messages, AgentRunStats* stats,
                  bool include_tools = true);

  ModelConfig config_;
  HttpFn http_;
  ToolRegistry& registry_;
  EnvFn env_;
  LogFn log_;
  LogFn say_;
  UrlPolicy policy_;
  SecretSet secrets_;
  RuntimeBudget budget_;
  ToolExecutor executor_;
  std::string api_key_override_;
};

}  // namespace ps5h
