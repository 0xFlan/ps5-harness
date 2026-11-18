#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ps5h/agent.h"
#include "ps5h/core.h"
#include "ps5h/policy.h"
#include "ps5h/runtime.h"
#include "ps5h/secrets.h"

namespace ps5h {

// Shared by every level of one delegation tree. The pool starts at the parent's
// step/token caps. A reservation is charged before a child starts; unused steps
// and reported tokens are refunded when it ends.
class DelegationBudget {
 public:
  DelegationBudget(int steps, long long tokens);

  bool reserve(int steps, long long tokens, std::string* error);
  void settle(int reserved_steps, long long reserved_tokens, int actual_steps,
              long long actual_tokens, bool usage_reported);

  int steps_remaining() const;
  long long tokens_remaining() const;

 private:
  mutable std::mutex mutex_;
  int steps_;
  long long tokens_;
};

struct DelegationLimits {
  int max_steps = 0;       // 0 = profile/default
  long long max_tokens = 0;  // 0 = profile/default
};

struct SwarmTask {
  std::string profile;
  std::string task;
  int max_steps = 0;
  long long max_tokens = 0;
};

struct DelegationConfig {
  std::string agents_dir;
  ModelConfig model;
  HttpFn http;
  EnvFn env;
  LogFn log;
  UrlPolicy policy;
  SecretSet secrets;
  RuntimeBudget budget;
  std::string parent_system_prompt;
  int parent_max_steps = 12;
  bool model_locked = false;       // --model wins over a child profile
  bool max_steps_locked = false;   // --max-steps wins over a child profile
  int max_depth = 2;
};

class Delegator : public std::enable_shared_from_this<Delegator> {
 public:
  explicit Delegator(DelegationConfig config);

  Json run(const std::string& profile_name, const std::string& task, int depth,
           const std::string& current_profile, const ToolRegistry& tools,
           const DelegationLimits& limits = DelegationLimits());

  // Runs independent child tasks in parallel, bounded by kMaxSwarmWidth and by
  // the same depth/budget/registry rules as delegate. Results preserve input
  // order; one failed child does not cancel the others.
  Json run_swarm(const std::vector<SwarmTask>& tasks, int depth,
                 const std::string& current_profile,
                 const ToolRegistry& tools);

 private:
  DelegationConfig config_;
  DelegationBudget budget_;
};

// Binds one delegate tool to a depth and to the registry it belongs to. The
// child registry is a copy of the parent tools minus delegate, plus a delegate
// bound to depth + 1. The weak reference lets an abandoned deadline handler
// outlive the run without touching a destroyed registry.
Tool make_delegate_tool(std::shared_ptr<Delegator> delegator, int depth,
                        std::string current_profile,
                        std::weak_ptr<const ToolRegistry> registry);

// Binds the swarm tool. Width is capped at 8; each child is a normal delegated
// run with the parent registry, so roots, policies and budgets still apply.
Tool make_swarm_tool(std::shared_ptr<Delegator> delegator, int depth,
                     std::string current_profile,
                     std::weak_ptr<const ToolRegistry> registry);

}  // namespace ps5h
