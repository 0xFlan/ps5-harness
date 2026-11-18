#include "ps5h/delegation.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "ps5h/agents.h"

namespace ps5h {
namespace {

Json delegate_failure(const std::string& profile, int depth,
                      const std::string& error, const DelegationBudget& budget) {
  Json out = Json::object();
  out["ok"] = false;
  out["profile"] = profile;
  out["depth"] = depth;
  out["error"] = error;
  out["remaining_steps"] = budget.steps_remaining();
  out["remaining_tokens"] = budget.tokens_remaining();
  return out;
}

std::string yes_no(bool value) { return value ? "true" : "false"; }

}  // namespace

DelegationBudget::DelegationBudget(int steps, long long tokens)
    : steps_(std::max(1, steps)), tokens_(std::max<long long>(0, tokens)) {}

bool DelegationBudget::reserve(int steps, long long tokens, std::string* error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (steps <= 0 || tokens < 0) {
    *error = "invalid delegation reservation";
    return false;
  }
  if (steps > steps_ || tokens > tokens_) {
    *error = "delegation budget exhausted (need " + std::to_string(steps) +
             " steps/" + std::to_string(tokens) + " tokens, have " +
             std::to_string(steps_) + "/" + std::to_string(tokens_) + ")";
    return false;
  }
  steps_ -= steps;
  tokens_ -= tokens;
  return true;
}

void DelegationBudget::settle(int reserved_steps, long long reserved_tokens,
                              int actual_steps, long long actual_tokens,
                              bool usage_reported) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (actual_steps < reserved_steps) {
    steps_ += reserved_steps - actual_steps;
  }
  if (!usage_reported) {
    // No provider usage: keep the full reservation charged. Conservative, and
    // it makes a missing usage block unable to unlock an unbounded loop.
    return;
  }
  if (actual_tokens < reserved_tokens) {
    tokens_ += reserved_tokens - actual_tokens;
  } else if (actual_tokens > reserved_tokens) {
    tokens_ -= actual_tokens - reserved_tokens;
  }
}

int DelegationBudget::steps_remaining() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return steps_;
}

long long DelegationBudget::tokens_remaining() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return tokens_;
}

Delegator::Delegator(DelegationConfig config)
    : config_(std::move(config)),
      budget_(std::max(1, config_.parent_max_steps),
              static_cast<long long>(config_.model.max_tokens > 0
                                         ? config_.model.max_tokens
                                         : 4096)) {}

Json Delegator::run(const std::string& profile_name, const std::string& task,
                    int depth, const std::string& current_profile,
                    const ToolRegistry& tools) {
  const int child_depth = depth + 1;
  if (config_.log) {
    config_.log("delegate call profile=" + profile_name + " from_depth=" +
                std::to_string(depth) + " max=" +
                std::to_string(config_.max_depth));
  }
  auto fail = [&](const std::string& error) {
    if (config_.log) {
      config_.log("delegate ! profile=" + profile_name + " depth=" +
                  std::to_string(child_depth) + " error=" + error);
    }
    return delegate_failure(profile_name, child_depth, error, budget_);
  };
  if (child_depth > config_.max_depth) {
    return fail("delegation depth limit reached (depth " +
                std::to_string(child_depth) + " > max " +
                std::to_string(config_.max_depth) + ")");
  }
  if (!current_profile.empty() && profile_name == current_profile) {
    return fail("an agent cannot delegate to itself");
  }

  const AgentProfileLoad loaded =
      load_agent_profile(config_.agents_dir, profile_name);
  if (!loaded.ok) {
    return fail("cannot load agent: " + loaded.error);
  }
  const AgentProfile& profile = loaded.profile;

  const int requested_steps =
      config_.max_steps_locked
          ? config_.parent_max_steps
          : (profile.has_max_steps ? profile.max_steps
                                   : config_.parent_max_steps);
  long long requested_tokens =
      profile.has_max_tokens && profile.max_tokens > 0
          ? profile.max_tokens
          : (config_.model.max_tokens > 0 ? config_.model.max_tokens : 4096);
  requested_tokens = std::min<long long>(requested_tokens, 32768);
  if (requested_steps < 1) {
    return fail("delegation step reservation is zero");
  }

  std::string budget_error;
  if (!budget_.reserve(requested_steps, requested_tokens, &budget_error)) {
    return fail(budget_error);
  }

  ModelConfig child_model = config_.model;
  if (!config_.model_locked && profile.has_model) {
    child_model.model = profile.model;
  }
  if (profile.has_temperature) {
    child_model.temperature = profile.temperature;
  }
  child_model.max_tokens = static_cast<int>(requested_tokens);

  const std::string child_prompt =
      profile.has_system_prompt ? profile.system_prompt
                                : config_.parent_system_prompt;

  ToolRegistry child_tools;
  for (const Tool& tool : tools.tools()) {
    if (tool.name == "delegate") {
      continue;
    }
    child_tools.add(tool);
  }
  child_tools.add(make_delegate_tool(shared_from_this(), child_depth,
                                     profile.name, &child_tools));

  if (config_.log) {
    config_.log("delegate -> profile=" + profile.name + " depth=" +
                std::to_string(child_depth) + " task=" + clip(task, 200));
  }

  auto answer = std::make_shared<std::string>();
  LogFn child_say = [answer, log = config_.log,
                     name = profile.name](const std::string& text) {
    *answer = text;
    if (log) {
      log("delegate answer from " + name + ": " + clip(text, 300));
    }
  };

  Agent child(child_model, config_.http, child_tools, config_.env, config_.log,
              child_say, config_.policy, config_.secrets, config_.budget);
  AgentOptions child_options;
  child_options.max_steps = requested_steps;
  child_options.system_prompt = child_prompt;

  AgentRunStats stats;
  const bool ok = child.run(task, child_options, &stats);
  budget_.settle(requested_steps, requested_tokens, stats.steps,
                 stats.total_tokens,
                 stats.usage_reported && stats.usage_complete);

  Json out = Json::object();
  out["ok"] = ok;
  out["profile"] = profile.name;
  out["depth"] = child_depth;
  out["steps"] = stats.steps;
  out["tokens"] = stats.total_tokens;
  out["usage_reported"] = stats.usage_reported;
  out["remaining_steps"] = budget_.steps_remaining();
  out["remaining_tokens"] = budget_.tokens_remaining();
  if (ok) {
    out["answer"] = *answer;
  } else {
    out["error"] = "delegate did not finish";
  }

  if (config_.log) {
    config_.log("delegate <- profile=" + profile.name + " ok=" + yes_no(ok) +
                " steps=" + std::to_string(stats.steps) + " tokens=" +
                std::to_string(stats.total_tokens) + " remaining=" +
                std::to_string(budget_.steps_remaining()) + "/" +
                std::to_string(budget_.tokens_remaining()));
  }
  return out;
}

Tool make_delegate_tool(std::shared_ptr<Delegator> delegator, int depth,
                        std::string current_profile,
                        const ToolRegistry* registry) {
  Tool tool;
  tool.name = "delegate";
  tool.description =
      "Run a subtask with another local agent profile. The child inherits this "
      "run's roots, URL policy, secrets, endpoints and plugins; profiles cannot "
      "widen them. Depth and steps/tokens are bounded.";
  Json properties = Json::object();
  properties["profile"] = Json::object();
  properties["profile"]["type"] = "string";
  properties["profile"]["description"] = "Local agent profile name";
  properties["task"] = Json::object();
  properties["task"]["type"] = "string";
  properties["task"]["description"] = "Subtask for the child agent";
  tool.parameters = Json::object();
  tool.parameters["type"] = "object";
  tool.parameters["properties"] = properties;
  tool.parameters["required"] = Json::array();
  tool.parameters["required"].push_back("profile");
  tool.parameters["required"].push_back("task");
  tool.timeout_ms = 3600000;
  tool.handler = [delegator = std::move(delegator), depth,
                  current_profile = std::move(current_profile),
                  registry](const Json& arguments) -> Json {
    const std::string profile = arguments["profile"].as_string();
    const std::string task = arguments["task"].as_string();
    if (profile.empty() || task.empty()) {
      Json out = Json::object();
      out["ok"] = false;
      out["error"] = "delegate requires profile and task";
      return out;
    }
    return delegator->run(profile, task, depth, current_profile, *registry);
  };
  return tool;
}

}  // namespace ps5h
