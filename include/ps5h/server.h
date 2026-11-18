#pragma once

#include <deque>
#include <mutex>
#include <string>

#include "ps5h/agent.h"
#include "ps5h/session.h"

namespace ps5h {

// Shared with the agent's log callback so the browser can poll live activity
// while a turn is running.
class ActivityLog {
 public:
  void add(const std::string& message);
  Json snapshot() const;
  void clear();

 private:
  mutable std::mutex mutex_;
  std::deque<std::string> entries_;
};

struct ServerOptions {
  int port = 8787;
  std::string token;
  std::string session_path;
  std::string build_id;
  std::string model;
  bool plan_mode = false;
  std::size_t context_chars = 120000;
};

// Serves the embedded single-page harness UI and its small JSON API. Blocks
// until the process is interrupted. Access requires the bearer token; the page
// itself is public so it can render the token prompt.
int run_server(const ServerOptions& options, Agent& agent, ChatSession& session,
               const AgentOptions& agent_options, const ToolRegistry& tools,
               const ActivityLog& activity, LogFn log);

}  // namespace ps5h
