// Host-side checks for the parts that do not touch the network or the console:
// JSON, dotted-path selection, tool naming, and manifest -> tool expansion.

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "ps5h/core.h"
#include "ps5h/endpoints.h"
#include "ps5h/json.h"

using namespace ps5h;

namespace {

void test_json_roundtrip() {
  Json value = Json::parse(R"({"a":1,"b":[true,null,"x"],"c":{"d":2.5}})");
  assert(value["a"].as_number() == 1);
  assert(value["b"].is_array());
  assert(value["b"][std::size_t{1}].is_null());
  assert(value["c"]["d"].as_number() == 2.5);

  Json reparsed = Json::parse(value.dump());
  assert(reparsed["b"][std::size_t{2}].as_string() == "x");
}

void test_pick() {
  Json value = Json::parse(R"({"response":{"docs":[{"identifier":"a"},{"identifier":"b"}]}})");
  assert(value.pick("response.docs").size() == 2);
  assert(value.pick("response.docs.1.identifier").as_string() == "b");
  assert(value.pick("response.docs.9").is_null());
  assert(value.pick("nope").is_null());
}

void test_tool_names() {
  assert(valid_tool_name("ep_archive_search"));
  assert(!valid_tool_name("bad name"));
  assert(sanitize_tool_name("Archive Search!") == "archive_search_");
  std::string long_name(80, 'a');
  assert(sanitize_tool_name(long_name).size() == 64);
}

void test_manifest_expansion() {
  const std::string manifest = R"({
    "id": "archive",
    "description": "Internet Archive",
    "baseUrl": "https://1.1.1.1",
    "auth": {"type": "none"},
    "operations": {
      "search": {
        "method": "GET",
        "path": "/advancedsearch.php",
        "query": {"q": "{{query}}", "rows": "{{rows}}"},
        "pick": "response.docs",
        "description": "Search"
      }
    }
  })";

  std::vector<HttpRequest> seen;
  HttpFn http = [&seen](const HttpRequest& request) {
    seen.push_back(request);
    HttpResponse response;
    response.status = 200;
    response.body = R"({"response":{"docs":[{"identifier":"item1"}]}})";
    return response;
  };

  ToolRegistry registry;
  EndpointCompiler compiler(http, [](const std::string&) { return std::string(); },
                            [](const std::string&) {});
  std::string error;
  assert(compiler.load_manifest(Json::parse(manifest), registry, &error));
  assert(registry.tools().size() == 1);

  const Tool* tool = registry.find("ep_archive_search");
  assert(tool != nullptr);
  assert(tool->parameters["required"].size() == 2);

  Json arguments = Json::object();
  arguments["query"] = "doom";
  arguments["rows"] = "2";
  Json result = tool->handler(arguments);

  assert(seen.size() == 1);
  assert(seen[0].url == "https://1.1.1.1/advancedsearch.php?q=doom&rows=2");
  assert(result["status"].as_number() == 200);
  assert(result["result"].size() == 1);
  assert(result["result"][std::size_t{0}]["identifier"].as_string() == "item1");
}

void test_manifest_missing_argument() {
  const std::string manifest = R"({
    "id": "x",
    "baseUrl": "https://1.1.1.1",
    "operations": {"get": {"path": "/{{id}}"}}
  })";

  bool called = false;
  HttpFn http = [&called](const HttpRequest&) {
    called = true;
    return HttpResponse{};
  };

  ToolRegistry registry;
  EndpointCompiler compiler(http, [](const std::string&) { return std::string(); },
                            [](const std::string&) {});
  std::string error;
  assert(compiler.load_manifest(Json::parse(manifest), registry, &error));

  Json result = registry.find("ep_x_get")->handler(Json::object());
  assert(!called);
  assert(result.as_string().find("missing required argument") != std::string::npos);
}

void test_auth_requires_credential() {
  const std::string manifest = R"({
    "id": "secure",
    "baseUrl": "https://1.1.1.1",
    "auth": {"type": "bearer", "env": "TEST_TOKEN"},
    "operations": {"status": {"path": "/status"}}
  })";

  ToolRegistry registry;
  EndpointCompiler compiler(
      [](const HttpRequest&) { return HttpResponse{}; },
      [](const std::string&) { return std::string(); },
      [](const std::string&) {});
  std::string error;
  assert(compiler.load_manifest(Json::parse(manifest), registry, &error));

  Json result = registry.find("ep_secure_status")->handler(Json::object());
  assert(result.as_string().find("TEST_TOKEN") != std::string::npos);
}

}  // namespace

int main() {
  test_json_roundtrip();
  test_pick();
  test_tool_names();
  test_manifest_expansion();
  test_manifest_missing_argument();
  test_auth_requires_credential();
  std::printf("core tests passed\n");
  return 0;
}
