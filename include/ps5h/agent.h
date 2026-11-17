#pragma once

#include <string>

#include "ps5h/core.h"
#include "ps5h/policy.h"
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

class Agent {
 public:
  Agent(ModelConfig config, HttpFn http, ToolRegistry& registry, EnvFn env, LogFn log,
        LogFn say, UrlPolicy policy, SecretSet secrets);

  bool run(const std::string& prompt, const AgentOptions& options);

 private:
  Json completion(const Json& messages);

  ModelConfig config_;
  HttpFn http_;
  ToolRegistry& registry_;
  EnvFn env_;
  LogFn log_;
  LogFn say_;
  UrlPolicy policy_;
  SecretSet secrets_;
};

}  // namespace ps5h
