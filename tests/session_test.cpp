#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "ps5h/agent.h"
#include "ps5h/core.h"
#include "ps5h/json.h"
#include "ps5h/policy.h"
#include "ps5h/secrets.h"
#include "ps5h/session.h"

using namespace ps5h;

namespace {

std::string make_temp_dir() {
  char pattern[] = "/tmp/ps5h-session-XXXXXX";
  char* made = ::mkdtemp(pattern);
  assert(made != nullptr);
  return std::string(made);
}

HttpResponse model_reply(const std::string& content) {
  Json message = Json::object();
  message["role"] = "assistant";
  message["content"] = content;
  Json choice = Json::object();
  choice["message"] = message;
  Json root = Json::object();
  root["choices"] = Json::array();
  root["choices"].push_back(choice);
  Json usage = Json::object();
  usage["prompt_tokens"] = 10;
  usage["completion_tokens"] = 5;
  usage["total_tokens"] = 15;
  root["usage"] = usage;
  HttpResponse response;
  response.status = 200;
  response.body = root.dump();
  return response;
}

}  // namespace

int main() {
  ToolRegistry registry;
  ModelConfig config;
  config.base_url = "http://127.0.0.1:9/v1";
  config.model = "stub";
  UrlPolicy policy;
  policy.https_only = false;
  policy.deny_private_ranges = false;
  SecretSet secrets;
  RuntimeBudget budget;
  budget.workers = 2;
  int calls = 0;
  HttpFn http = [&calls](const HttpRequest& request) -> HttpResponse {
    ++calls;
    const Json input = Json::parse(request.body);
    const Json& messages = input["messages"];
    std::string transcript;
    for (const Json& message : messages.items()) {
      transcript += message["content"].as_string();
    }
    if (transcript.find("Compress the conversation") != std::string::npos) {
      return model_reply("dense summary of earlier work");
    }
    return model_reply(std::string(3200, 'x') + " answer " +
                       std::to_string(calls));
  };
  LogFn quiet = [](const std::string&) {};
  Agent agent(config, http, registry, [](const std::string&) { return std::string(); },
              quiet, quiet, policy, secrets, budget);

  AgentOptions options;
  options.max_steps = 4;
  options.system_prompt = "system prompt for the session test";
  ChatSession session;
  session.reset(options.system_prompt);
  session.context_chars = 4096;

  std::string answer;
  std::string error;
  for (int i = 0; i < 3; ++i) {
    assert(session.send(agent, "question " + std::to_string(i + 1), options,
                        &answer, &error));
    assert(error.empty());
    assert(!answer.empty());
  }
  assert(calls >= 4);  // three turns plus at least one summary
  assert(session.compactions >= 1);
  assert(session.messages[0]["role"].as_string() == "system");
  assert(session.messages[1]["role"].as_string() == "system");
  assert(session.messages[1]["content"].as_string().find("summary") !=
         std::string::npos);
  assert(session.approximate_chars() < 12000);

  const std::string dir = make_temp_dir();
  const std::string path = dir + "/session.json";
  assert(session.save(path, &error));
  ChatSession loaded;
  loaded.context_chars = 2048;
  assert(loaded.load(path, options.system_prompt, &error));
  assert(loaded.message_count() == session.message_count());
  assert(loaded.compactions == session.compactions);
  assert(loaded.messages[0]["content"].as_string() == options.system_prompt);

  // An empty path means "do not persist", not an error.
  assert(session.save("", &error));
  assert(error.empty());

  // Missing sessions are reported but leave a usable empty conversation.
  ChatSession fresh;
  assert(!fresh.load(dir + "/missing.json", options.system_prompt, &error));
  assert(!error.empty());
  // message_count() counts conversation messages, not the system prompt.
  assert(fresh.messages.size() == 1);
  assert(fresh.message_count() == 0);
  assert(fresh.messages[0]["role"].as_string() == "system");

  std::printf("session_test: ok (%d provider calls, %zu compactions)\n", calls,
              session.compactions);
  return 0;
}
