/*
 * PS5 Harness plugin ABI, version 2.
 *
 * A plugin is a shared object (libfoo.so on the console, libfoo.dylib on a host
 * build) that exports:
 *
 *   int         ps5h_plugin_init(const ps5h_host_api *host);
 *   const char *ps5h_plugin_id(void);
 *
 * ps5h_plugin_init runs once, at load time. Register every tool there; the
 * handles stay valid for the process lifetime. Return 0 on success.
 *
 * Tool handlers receive the model's arguments as a JSON object string, and
 * deliver their result through the emit callback:
 *
 *   emit(tool_user, "{\"result\":42}", 13);
 *
 * The host owns that buffer. A plugin must never allocate memory for the host to
 * free: version 1 did that and it corrupts the heap whenever the plugin links a
 * different C runtime than the harness. Calling emit more than once replaces the
 * result; calling it never is a tool error.
 *
 * The struct is append-only: new members go at the end and abi_version is
 * bumped. Check host->abi_version before using newer members.
 */
#ifndef PS5H_PLUGIN_API_H
#define PS5H_PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS5H_PLUGIN_ABI_VERSION 2u

typedef void (*ps5h_emit_fn)(void *tool_user, const char *json, size_t length);

typedef void (*ps5h_tool_fn)(void *tool_user, const char *arguments_json,
                             ps5h_emit_fn emit);

typedef struct ps5h_host_api {
  uint32_t abi_version;
  void *user;

  /* level is one of "debug", "info", "warn", "error". */
  void (*log)(void *user, const char *level, const char *message);

  /* parameters_json must be a JSON Schema object string. Returns 0 on success. */
  int (*register_tool)(void *user, const char *name, const char *description,
                       const char *parameters_json, ps5h_tool_fn handler,
                       void *tool_user);

  /* Returns NULL when the variable is unset. Do not cache across calls. */
  const char *(*get_env)(void *user, const char *name);

  /*
   * Performs an HTTP request so plugins never link a TLS stack themselves.
   * request_json:  {"method":"GET","url":"...","headers":{"k":"v"},"body":"..."}
   * Returns a malloc()ed JSON string {"status":200,"body":"...","error":""},
   * or NULL. This buffer is the host's, not the plugin's: free it with
   * free_string().
   */
  char *(*http_request)(void *user, const char *request_json);
  void (*free_string)(void *user, char *value);
} ps5h_host_api;

int ps5h_plugin_init(const ps5h_host_api *host);
const char *ps5h_plugin_id(void);

#ifdef __cplusplus
}
#endif

#endif /* PS5H_PLUGIN_API_H */
