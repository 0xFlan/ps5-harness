#include "ps5h/agent.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ps5h {
Agent::Agent(ModelConfig config, HttpFn http, ToolRegistry& registry, EnvFn env, LogFn log,
             LogFn say, UrlPolicy policy, SecretSet secrets, RuntimeBudget budget)
    : config_(std::move(config)),
      http_(std::move(http)),
      registry_(registry),
      env_(std::move(env)),
      log_(std::move(log)),
      say_(std::move(say)),
      policy_(std::move(policy)),
      secrets_(std::move(secrets)),
      budget_(budget),
      executor_(budget.workers) {}

Json Agent::completion(const Json& messages) {
  if (!http_) {
    log_("no HTTP backend available; cannot call the model");
    return Json();
  }

  Json body = Json::object();
  body["model"] = config_.model;
  body["messages"] = messages;
  body["temperature"] = config_.temperature;
  if (config_.max_tokens > 0) {
    body["max_tokens"] = static_cast<long long>(config_.max_tokens);
  }
  Json tools = registry_.schema_list();
  if (tools.size() > 0) {
    body["tools"] = tools;
    body["tool_choice"] = "auto";
  }

  HttpRequest request;
  request.method = "POST";
  request.url = config_.base_url;
  if (!request.url.empty() && request.url.back() == '/') {
    request.url.pop_back();
  }
  request.url += "/chat/completions";

  // The provider endpoint is where the API key travels, so it is subject to the
  // same policy as everything else: no cleartext, no private ranges.
  const UrlPolicy::Decision endpoint = policy_.check(request.url);
  if (!endpoint.allowed) {
    log_("model endpoint rejected by URL policy: " + endpoint.reason);
    return Json();
  }
  request.resolved_ip = endpoint.resolved_ip;
  request.max_body_bytes = budget_.buffered_response_bytes;

  request.headers.emplace_back("Content-Type", "application/json");

  if (env_) {
    std::string key = env_(config_.api_key_env);
    if (!key.empty()) {
      request.headers.emplace_back("Authorization", "Bearer " + key);
    }
  }
  request.body = body.dump();

  HttpResponse response = http_(request);
  if (!response.error.empty()) {
    log_("model request failed: " + response.error);
    return Json();
  }
  if (response.status < 200 || response.status >= 300) {
    log_("model returned HTTP " + std::to_string(response.status) + ": " +
         clip(response.body, 600));
    return Json();
  }

  Json parsed;
  try {
    parsed = Json::parse(response.body);
  } catch (const JsonError& failure) {
    log_(std::string("model returned invalid JSON: ") + failure.what());
    return Json();
  }

  const Json& message = parsed["choices"][std::size_t{0}]["message"];
  if (!message.is_object()) {
    log_("model response had no choices[0].message");
    return Json();
  }
  return message;
}

bool Agent::run(const std::string& prompt, const AgentOptions& options) {
  Json messages = Json::array();

  Json system = Json::object();
  system["role"] = "system";
  system["content"] = options.system_prompt;
  messages.push_back(system);

  Json user = Json::object();
  user["role"] = "user";
  user["content"] = prompt;
  messages.push_back(user);

  for (int step = 0; step < options.max_steps; ++step) {
    Json message = completion(messages);
    if (!message.is_object()) {
      return false;
    }
    messages.push_back(message);

    const Json& calls = message["tool_calls"];
    if (!calls.is_array() || calls.size() == 0) {
      std::string text = message["content"].as_string();
      say_(secrets_.redact(text.empty() ? "(no content)" : text));
      return true;
    }

    // A turn can carry several independent tool calls. They run across the
    // executor sized from the measured CPU count, but their messages are still
    // appended in the provider's order.
    std::vector<ToolJob> jobs;
    std::vector<std::string> results(calls.size());
    for (std::size_t i = 0; i < calls.size(); ++i) {
      const Json& call = calls[i];
      const std::string name = call["function"]["name"].as_string();
      const std::string raw = call["function"]["arguments"].as_string("{}");

      Json arguments = Json::object();
      if (!raw.empty()) {
        try {
          arguments = Json::parse(raw);
        } catch (const JsonError& failure) {
          results[i] = std::string("invalid tool arguments: ") + failure.what();
          continue;
        }
      }

      const Tool* tool = registry_.find(name);
      if (tool == nullptr) {
        results[i] = "unknown tool: " + name;
        continue;
      }
      if (!tool->handler) {
        results[i] = "tool " + name + " has no handler";
        continue;
      }

      log_("tool " + name + " " + clip(arguments.dump(), 300));
      ToolJob job;
      job.index = i;
      job.handler = tool->handler;
      job.arguments = std::move(arguments);
      job.timeout_ms = tool->timeout_ms;
      jobs.push_back(std::move(job));
    }

    const std::vector<ToolOutcome> outcomes = executor_.run(jobs);
    for (const ToolOutcome& outcome : outcomes) {
      std::string content;
      if (!outcome.error.empty()) {
        content = "tool error: " + outcome.error;
      } else {
        content = outcome.result.is_string() ? outcome.result.as_string() : outcome.result.dump();
      }
      // Single choke point: nothing leaves for the provider unredacted.
      results[outcome.index] = secrets_.redact(content);
    }

    for (std::size_t i = 0; i < calls.size(); ++i) {
      Json tool_message = Json::object();
      tool_message["role"] = "tool";
      tool_message["tool_call_id"] = calls[i]["id"].as_string();
      tool_message["content"] = clip(results[i], budget_.tool_output_bytes);
      messages.push_back(tool_message);
    }
  }

  log_("stopped at max steps (" + std::to_string(options.max_steps) + ")");
  return false;
}

}  // namespace ps5h
