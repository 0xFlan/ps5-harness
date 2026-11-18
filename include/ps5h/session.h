#pragma once

#include <cstddef>
#include <string>

#include "ps5h/agent.h"

namespace ps5h {

// A persistent conversation for the LAN UI. It owns the OpenAI-style message
// array so tool calls and their results survive across user turns.
//
// Context discipline: the transcript is measured approximately (bytes / 4) and
// compacted by asking the model for a dense summary only when it would exceed
// the configured budget. The newest user turns are always kept verbatim.
struct ChatSession {
  Json messages = Json::array();
  std::string system_prompt;
  std::size_t context_chars = 120000;
  std::size_t compactions = 0;
  AgentRunStats last_stats;

  void reset(const std::string& prompt);
  bool send(Agent& agent, const std::string& prompt, const AgentOptions& options,
            std::string* answer, std::string* error);
  bool compact(Agent& agent, const AgentOptions& options, std::string* error);

  std::size_t approximate_chars() const;
  std::size_t message_count() const {
    std::size_t count = 0;
    for (const Json& message : messages.items()) {
      if (message["role"].as_string() != "system") ++count;
    }
    return count;
  }

  bool save(const std::string& path, std::string* error) const;
  bool load(const std::string& path, const std::string& default_prompt,
            std::string* error);
};

}  // namespace ps5h
