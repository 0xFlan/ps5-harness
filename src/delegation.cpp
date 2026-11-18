#include "ps5h/delegation.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

constexpr std::size_t kMaxSwarmWidth = 8;

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
                    const ToolRegistry& tools,
                    const DelegationLimits& limits) {
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

  int requested_steps =
      config_.max_steps_locked
          ? config_.parent_max_steps
          : (profile.has_max_steps ? profile.max_steps
                                   : config_.parent_max_steps);
  if (limits.max_steps > 0) {
    requested_steps = limits.max_steps;
  }
  requested_steps = std::min(requested_steps, 64);
  long long requested_tokens =
      profile.has_max_tokens && profile.max_tokens > 0
          ? profile.max_tokens
          : (config_.model.max_tokens > 0 ? config_.model.max_tokens : 4096);
  if (limits.max_tokens > 0) {
    requested_tokens = limits.max_tokens;
  }
  requested_tokens = std::max<long long>(1, requested_tokens);
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

  auto child_tools = std::make_shared<ToolRegistry>();
  for (const Tool& tool : tools.tools()) {
    if (tool.name == "delegate" || tool.name == "swarm") {
      continue;
    }
    child_tools->add(tool);
  }
  child_tools->add(make_delegate_tool(shared_from_this(), child_depth,
                                      profile.name, child_tools));
  child_tools->add(make_swarm_tool(shared_from_this(), child_depth,
                                   profile.name, child_tools));

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

  Agent child(child_model, config_.http, *child_tools, config_.env, config_.log,
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

Json Delegator::run_swarm(const std::vector<SwarmTask>& tasks, int depth,
                          const std::string& current_profile,
                          const ToolRegistry& tools) {
  const int child_depth = depth + 1;
  auto fail = [&](const std::string& error) {
    Json out = Json::object();
    out["ok"] = false;
    out["depth"] = child_depth;
    out["error"] = error;
    out["remaining_steps"] = budget_.steps_remaining();
    out["remaining_tokens"] = budget_.tokens_remaining();
    if (config_.log) {
      config_.log("swarm ! depth=" + std::to_string(child_depth) +
                  " error=" + error);
    }
    return out;
  };
  if (child_depth > config_.max_depth) {
    return fail("delegation depth limit reached (depth " +
                std::to_string(child_depth) + " > max " +
                std::to_string(config_.max_depth) + ")");
  }
  if (tasks.empty()) {
    return fail("swarm requires at least one task");
  }
  if (tasks.size() > kMaxSwarmWidth) {
    return fail("swarm width " + std::to_string(tasks.size()) +
                " exceeds the hard limit of " +
                std::to_string(kMaxSwarmWidth));
  }
  if (config_.log) {
    config_.log("swarm start depth=" + std::to_string(child_depth) +
                " width=" + std::to_string(tasks.size()));
  }

  std::vector<Json> results(tasks.size());
  std::vector<std::thread> threads;
  threads.reserve(tasks.size());
  for (std::size_t i = 0; i < tasks.size(); ++i) {
    try {
      threads.emplace_back([this, &tasks, &results, i, depth,
                            current_profile, &tools]() {
        const SwarmTask& task = tasks[i];
        DelegationLimits limits;
        limits.max_steps = task.max_steps;
        limits.max_tokens = task.max_tokens;
        results[i] = run(task.profile, task.task, depth, current_profile,
                         tools, limits);
      });
    } catch (const std::exception& error) {
      Json out = Json::object();
      out["ok"] = false;
      out["profile"] = tasks[i].profile;
      out["depth"] = child_depth;
      out["error"] = std::string("cannot start swarm worker: ") + error.what();
      out["remaining_steps"] = budget_.steps_remaining();
      out["remaining_tokens"] = budget_.tokens_remaining();
      results[i] = std::move(out);
    }
  }
  for (std::thread& thread : threads) {
    thread.join();
  }

  bool all_ok = true;
  Json lanes = Json::array();
  for (const Json& result : results) {
    all_ok = all_ok && result["ok"].as_bool(false);
    lanes.push_back(result);
  }
  Json out = Json::object();
  out["ok"] = all_ok;
  out["depth"] = child_depth;
  out["width"] = static_cast<long long>(tasks.size());
  out["swarm"] = std::move(lanes);
  out["remaining_steps"] = budget_.steps_remaining();
  out["remaining_tokens"] = budget_.tokens_remaining();
  if (config_.log) {
    config_.log("swarm <- ok=" + yes_no(all_ok) + " width=" +
                std::to_string(tasks.size()) + " remaining=" +
                std::to_string(budget_.steps_remaining()) + "/" +
                std::to_string(budget_.tokens_remaining()));
  }
  return out;
}

Tool make_delegate_tool(std::shared_ptr<Delegator> delegator, int depth,
                        std::string current_profile,
                        std::weak_ptr<const ToolRegistry> registry) {
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
    const std::shared_ptr<const ToolRegistry> tools = registry.lock();
    if (!tools) {
      Json out = Json::object();
      out["ok"] = false;
      out["error"] = "delegate tool registry is no longer available";
      return out;
    }
    return delegator->run(profile, task, depth, current_profile, *tools);
  };
  return tool;
}

Tool make_swarm_tool(std::shared_ptr<Delegator> delegator, int depth,
                     std::string current_profile,
                     std::weak_ptr<const ToolRegistry> registry) {
  Tool tool;
  tool.name = "swarm";
  tool.description =
      "Run up to 8 independent subtasks in parallel with local agent profiles. "
      "Each lane is a normal delegated child and inherits the same roots, URL "
      "policy, secrets, endpoints, plugins and shared step/token budget. Use "
      "this for orthogonal local-recon lanes, not for repeating one question.";
  Json task_properties = Json::object();
  task_properties["profile"] = Json::object();
  task_properties["profile"]["type"] = "string";
  task_properties["task"] = Json::object();
  task_properties["task"]["type"] = "string";
  task_properties["max_steps"] = Json::object();
  task_properties["max_steps"]["type"] = "number";
  task_properties["max_steps"]["description"] = "Optional lane step budget";
  task_properties["max_tokens"] = Json::object();
  task_properties["max_tokens"]["type"] = "number";
  task_properties["max_tokens"]["description"] = "Optional lane token budget";
  Json task_schema = Json::object();
  task_schema["type"] = "object";
  task_schema["properties"] = task_properties;
  task_schema["required"] = Json::array();
  task_schema["required"].push_back("profile");
  task_schema["required"].push_back("task");

  Json parameters = Json::object();
  parameters["type"] = "object";
  Json properties = Json::object();
  properties["tasks"] = Json::object();
  properties["tasks"]["type"] = "array";
  properties["tasks"]["description"] = "Independent lanes (1-8)";
  properties["tasks"]["items"] = std::move(task_schema);
  parameters["properties"] = std::move(properties);
  parameters["required"] = Json::array();
  parameters["required"].push_back("tasks");
  tool.parameters = std::move(parameters);
  tool.timeout_ms = 3600000;
  tool.handler = [delegator = std::move(delegator), depth,
                  current_profile = std::move(current_profile),
                  registry](const Json& arguments) -> Json {
    const Json& list = arguments["tasks"];
    if (!list.is_array() || list.size() == 0) {
      Json out = Json::object();
      out["ok"] = false;
      out["error"] = "swarm requires a non-empty tasks array";
      return out;
    }
    if (list.size() > kMaxSwarmWidth) {
      Json out = Json::object();
      out["ok"] = false;
      out["error"] = "swarm width exceeds the hard limit of " +
                     std::to_string(kMaxSwarmWidth);
      return out;
    }
    std::vector<SwarmTask> tasks;
    tasks.reserve(list.size());
    for (const Json& item : list.items()) {
      if (!item.is_object()) {
        Json out = Json::object();
        out["ok"] = false;
        out["error"] = "each swarm task must be an object";
        return out;
      }
      SwarmTask task;
      task.profile = item["profile"].as_string();
      task.task = item["task"].as_string();
      if (task.profile.empty() || task.task.empty()) {
        Json out = Json::object();
        out["ok"] = false;
        out["error"] = "each swarm task requires profile and task";
        return out;
      }
      const double requested_steps = item["max_steps"].as_number(0);
      int steps = 0;
      if (std::isfinite(requested_steps) && requested_steps > 0.0) {
        steps = requested_steps > 64.0 ? 64 : static_cast<int>(requested_steps);
      }
      task.max_steps = steps;
      const double requested_tokens = item["max_tokens"].as_number(0);
      long long tokens = 0;
      if (std::isfinite(requested_tokens) && requested_tokens > 0.0) {
        tokens = requested_tokens > 32768.0
                     ? 32768
                     : static_cast<long long>(requested_tokens);
      }
      task.max_tokens = tokens;
      tasks.push_back(std::move(task));
    }
    const std::shared_ptr<const ToolRegistry> tools = registry.lock();
    if (!tools) {
      Json out = Json::object();
      out["ok"] = false;
      out["error"] = "swarm tool registry is no longer available";
      return out;
    }
    return delegator->run_swarm(tasks, depth, current_profile, *tools);
  };
  return tool;
}

}  // namespace ps5h
