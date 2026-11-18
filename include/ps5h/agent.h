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
  std::string api_key_env = "PS5H_API_KEY";
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
  int workers() const { return executor_.workers(); }

 private:
  Json completion(const Json& messages, AgentRunStats* stats);

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
};

}  // namespace ps5h
