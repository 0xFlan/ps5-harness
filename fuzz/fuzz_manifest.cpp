// Fuzz target: the manifest compiler turns a stranger's JSON into tools. The
// manifest is generated here (rather than parsed from the input) so the base URL
// stays a fixed public literal and the URL policy never performs a DNS lookup.

#include <cstddef>
#include <cstdint>
#include <string>

#include "ps5h/core.h"
#include "ps5h/endpoints.h"
#include "ps5h/json.h"
#include "ps5h/policy.h"

namespace {

std::string as_text(const std::uint8_t* data, std::size_t size, std::size_t limit) {
  std::string out;
  out.reserve(size < limit ? size : limit);
  for (std::size_t i = 0; i < size && i < limit; ++i) {
    // Keep the alphabet printable-ish so substitutions get exercised.
    out.push_back(static_cast<char>(0x20 + (data[i] % 0x5F)));
  }
  return out;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size < 4) {
    return 0;
  }

  const std::string id = as_text(data, size / 4, 48);
  const std::string path = "/v1/" + as_text(data, size / 4, 96);
  const std::string pick = as_text(data + size / 2, size / 4, 64);
  const std::string placeholder = as_text(data + size / 2, size - size / 2, 16);

  ps5h::Json manifest = ps5h::Json::object();
  manifest["id"] = id;
  manifest["baseUrl"] = "https://1.1.1.1";
  manifest["description"] = "fuzz";

  ps5h::Json operation = ps5h::Json::object();
  operation["method"] = "GET";
  operation["path"] = path + "/{{" + placeholder + "}}";
  operation["pick"] = pick;

  ps5h::Json query = ps5h::Json::object();
  query["q"] = "{{" + placeholder + "}}";
  operation["query"] = query;

  ps5h::Json operations = ps5h::Json::object();
  operations["op"] = operation;
  manifest["operations"] = operations;

  ps5h::UrlPolicy policy;
  ps5h::ToolRegistry registry;
  ps5h::EndpointCompiler compiler(
      [](const ps5h::HttpRequest&) {
        ps5h::HttpResponse response;
        response.status = 200;
        response.body = "{\"a\":[1,2,3],\"b\":{\"c\":true}}";
        return response;
      },
      [](const std::string&) { return std::string("token-value-123456"); },
      [](const std::string&) {}, policy);

  std::string error;
  if (!compiler.load_manifest(manifest, registry, &error)) {
    return 0;
  }

  for (const auto& tool : registry.tools()) {
    ps5h::Json arguments = ps5h::Json::object();
    for (const auto& property : tool.parameters["properties"].fields()) {
      arguments[property.first] = "value";
    }
    (void)tool.handler(arguments);
  }
  return 0;
}
