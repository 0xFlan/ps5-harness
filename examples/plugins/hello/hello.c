/*
 * Minimal PS5 Harness plugin (ABI v2). Build:
 *
 *   make plugin-example
 *   ./build/harness --tools --plugins examples/plugins/hello
 *
 * A plugin may call host->http_request() instead of linking its own TLS stack,
 * and host->get_env() for credentials. Results go through the emit callback:
 * the host owns that buffer, so nothing is allocated across the boundary.
 */
#include "ps5h/plugin_api.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static void echo_handler(void *tool_user, const char *arguments_json,
                         ps5h_emit_fn emit) {
  (void)tool_user;
  const char *payload = (arguments_json != NULL && arguments_json[0] != '\0')
                            ? arguments_json
                            : "{}";
  char buffer[4096];
  int written = snprintf(buffer, sizeof buffer, "{\"echo\":%s}", payload);
  if (written < 0) {
    return;
  }
  if ((size_t)written >= sizeof buffer) {
    written = (int)sizeof buffer - 1;
  }
  emit(tool_user, buffer, (size_t)written);
}

int ps5h_plugin_init(const ps5h_host_api *host) {
  if (host == NULL || host->abi_version < 2) {
    return -1;
  }
  int result = host->register_tool(
      host->user,
      "hello_echo",
      "Echo back the arguments it was given. Useful for testing the plugin path.",
      "{\"type\":\"object\",\"properties\":{\"value\":{\"type\":\"string\"}}}",
      echo_handler,
      NULL);
  if (result != 0) {
    return result;
  }
  host->log(host->user, "info", "hello plugin registered hello_echo");
  return 0;
}

const char *ps5h_plugin_id(void) { return "hello"; }
