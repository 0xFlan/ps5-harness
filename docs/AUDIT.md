# Audit — confirmed defects

Method: read the shipped sources, traced each external input (model arguments,
manifest JSON, HTTP response, plugin return value) to its sink (filesystem,
network, memory, model context). Nothing here is hypothetical; each item names
the line and the scenario.

Severity: **S1** = key exfiltration, filesystem escape, or remote code path.
**S2** = crash, hang, or resource exhaustion. **S3** = hygiene, test gap.

| # | Issue | Severity | Status |
|---|---|---|---|
| A1 | Write-root escape via prefix compare | S1 | **Closed** — `src/paths.cpp`, `test_path_guard` |
| A2 | Secrets readable by `fs_*` tools | S1 | **Closed** — `src/paths.cpp`, `src/secrets.cpp`, `test_secret_redaction` |
| A3 | Whole-body buffering → OOM | S2 | **Closed** — `src/http.cpp`, `test_body_cap`, `test_streaming_download` |
| A4 | Unvalidated manifest `baseUrl` → file read / SSRF | S1 | **Closed** — `src/policy.cpp`, `test_url_policy` |
| A5 | Plugin memory freed by the host | S2 | **Closed** — ABI v2 `emit()` into host memory |
| A6 | No per-tool deadline | S2 | **Closed** — `run_with_deadline`, `test_tool_deadline` |
| A7 | Untrusted parsers have no fuzz corpus | S2 | **Closed** — `fuzz/`, `make fuzz-run`, 20k iterations ASan clean |
| A8 | No repo, license, CI, or TLS policy | S3 | **Partial** — MIT license, public GitHub repository and green Ubuntu/macOS CI; CA-bundle/clock policy for console TLS remains open |

---

## A1 — Write-root escape (S1) — CLOSED

**Fixed by:** `src/paths.cpp` (`canonical_path`, `PathGuard::allows`). The old
`src/main.cpp` `under_roots` helper is gone.

**Where (as found):** `src/main.cpp:49-56` (`under_roots`), used at `src/main.cpp:145`.

```cpp
if (resolved.compare(0, root.size(), root) == 0) { return true; }
```

**Why it's wrong:** a raw prefix compare accepts any sibling whose name starts
with the root. With `allowed_roots = ["/data/homebrew"]`, all of these pass:
`/data/homebrewX/evil`, `/data/homebrew.bak/x`. Symlinks are never resolved, so
a symlink inside an allowed root pointing at `/` also passes. `..` segments are
untouched.

**Exploit shape:** the model is asked to "save this config" and a fetched page
influences the path. One `fs_write` outside the sandbox, no elevation needed.

**Fix spec:**
1. Canonicalise the root once at startup with `realpath()`; a root that fails
   canonicalisation is a startup error, not a silent skip.
2. Canonicalise the target. If it doesn't exist, canonicalise its nearest
   existing ancestor and re-append the remaining components, rejecting `.` and
   `..` outright.
3. Accept only when `target == root` or `target` starts with `root + "/"`.
4. Resolve symlinks on the final component (`realpath` on the fully written
   path after `open` with `O_NOFOLLOW`, or `lstat` before rename).
5. Reject any component that is not valid UTF-8 or contains a NUL.

**Test that proves it:** `under_roots("/data/homebrewX/evil") == false`,
`under_roots("/data/homebrew/../etc/x") == false`, symlink-to-`/` case false,
legit nested path true.

---

## A2 — Secrets readable by `fs_*` tools (S1)

**Where:** `src/main.cpp` `fs_read`/`fs_list` handlers — no root check at all;
`fs_write` is the only guarded tool.

**Why it matters:** the API key lives in the environment, but the config file
path, logs, and any token file the user drops in the app folder are readable.
A tool result goes into the message array and is sent to the model provider. So
"read `config.json`" is a key-exfiltration primitive that needs no malicious
plugin — a prompt-injected instruction is enough.

**Fix spec:**
1. Reads restricted to the same allowlist as writes, plus an explicit
   `read_roots` list so a user can expose a library folder without exposing the
   app folder.
2. Hard denylist evaluated *after* canonicalisation: the app's config, log,
   state and plugin directories, `sce_system`, any path containing `.key`,
   `.pem`, `token`, `secret`, `credential`.
3. Output redaction pass: any value matching an env-var value known to the
   process is replaced with `[redacted]` before the result enters the message
   array. Applies to every tool, not just `fs_read`.
4. `fs_list` returns names only for denied directories, never contents.

**Test that proves it:** a file containing the value of `PS5H_API_KEY` written
into an allowed root is not returned by `fs_read`; the variable's value never
appears in any tool result.

---

## A3 — Whole-body buffering (S2)

**Where:** `src/http.cpp:21` (`write_callback` appends to `std::string`),
`src/http.cpp:49` (`CURLOPT_WRITEDATA` points at the response body).

**Why it matters:** every response is materialised in RAM. `http_get` on a
large file, or an endpoint manifest pointing at a big payload, allocates the
whole body. On a console with a few hundred MB of usable address space per
title, a 4 GB image is an instant OOM kill — and the process has no way to
report why.

**Fix spec:**
1. Split the interface: `HttpFn` keeps the buffered form for JSON APIs with a
   hard `max_body_bytes` (default 4 MB, error past it); add
   `HttpDownload { open(url) → handle; write_to(fd); progress(cb); cancel(); }`
   that streams to a file descriptor.
2. The download path writes in 64 KB chunks, never holding more than one chunk
   plus curl's internal buffer.
3. Progress callback reports bytes and total; cancellation aborts via
   `CURLOPT_XFERINFOFUNCTION`.
4. Response headers captured for `Content-Length`, `ETag`, `Accept-Ranges`.

**Test that proves it:** a local fixture server streams 256 MB; peak RSS stays
under a fixed budget (measured, not asserted by eye) and the file lands
byte-identical.

---

## A4 — Manifest `baseUrl` is unvalidated (S1)

**Where:** `src/endpoints.cpp:113-119` (read and only checked for non-empty).

**Why it matters:** a manifest is data a stranger may have written. As shipped:
- `file:///etc/passwd` on a `path` of `/etc/passwd` makes curl read local files.
- `http://` silently downgrades to cleartext.
- `http://192.168.1.1/...` or `http://127.0.0.1:8080` turns the console into an
  SSRF probe of the user's own LAN, from a machine that is often trusted by it.
- Redirects are followed (`CURLOPT_FOLLOWLOCATION`) across hosts, so a benign
  allowlisted URL can bounce anywhere.

**Fix spec:**
1. Scheme must be `https`. Reject everything else at load time with a message
   naming the file and the value.
2. Optional `allowed_hosts` in config. Unset means "any public host"; when set,
   the manifest host must be in it.
3. Deny by default: loopback, link-local, private ranges (10/8, 172.16/12,
   192.168/16, 169.254/16), `::1`, unique-local IPv6, and raw IP literals
   unless explicitly allowlisted.
4. Resolve the host and re-check the resolved address (DNS-rebinding guard);
   pin the connection to that address via `CURLOPT_RESOLVE` for the request.
5. Redirects: `CURLOPT_MAXREDIRS 3` and re-validate the redirect target against
   the same policy, or disable redirects entirely for manifests (preferred).
6. Cap response size (`max_body_bytes`) and total request time.

**Test that proves it:** manifests with `file://`, `http://`, `127.0.0.1`,
`192.168.0.1`, and a redirect to an off-policy host are all rejected with the
policy reason, before any socket opens.

---

## A5 — Plugin memory freed by the host (S2)

**Where:** `src/plugins.cpp`, the tool bridge: the plugin returns a `malloc`ed
string and the host calls `std::free(result)`.

**Why it matters:** this works only while plugin and host share one allocator.
A plugin linked against a different libc, a static CRT, or a bundled allocator
makes that `free()` undefined behaviour — usually a crash far from the cause,
sometimes heap corruption.

**Fix spec:** pick one and document it in the ABI header:
- **Preferred:** host-owned buffer. Pass a `write_result(void* user, const char*
  json)` callback plus a `size_t`, so the plugin never allocates across the
  boundary. Breaks ABI v1, so do it *before* P5 freezes the ABI.
- **Fallback:** keep `malloc`/`free` but require the plugin to build with the
  same libc the host links, and say so in the template's build file. Add a
  conformance test plugin that allocates 10,000 strings to shake out mismatches.

**Test that proves it:** the ABI conformance plugin allocates and returns
results in a tight loop under ASan with no allocator diagnostics.

---

## A6 — No per-tool deadline (S2)

**Where:** `src/agent.cpp` — `max_steps` bounds iterations, but a single
handler can block forever.

**Fix spec:** run handlers with a deadline (default 30 s, per-tool override from
the manifest, downloads exempt and governed by an inactivity timeout instead).
On expiry return a tool error string rather than killing the process. Plugin
handlers that cannot be interrupted safely get a documented "must return
promptly" contract plus a watchdog that logs a stack hint.

**Test that proves it:** a plugin that sleeps 120 s returns a timeout error at
the deadline and the loop continues to the next step.

---

## A7 — Untested untrusted input (S2)

**Where:** `src/json.cpp` (hand-written recursive-descent parser, ~330 lines,
recursion capped at 128), `src/endpoints.cpp` (manifest compiler). No fuzz
corpus. `src/agent.cpp`, `src/http.cpp`, and the plugin ABI have no tests.

**Fix spec:**
1. libFuzzer targets: `fuzz_json`, `fuzz_manifest` (parse + compile + invoke
   with generated arguments). Run 10 minutes each with ASan+UBSan in CI's slow
   lane.
2. Seeded corpus from the real examples plus adversarial cases: deep nesting,
   huge arrays, `\u0000`, invalid UTF-8, duplicated keys, `{{}}` placeholders,
   64 KB tool names.
3. Integration test against a local fixture HTTP server (no network in CI):
   status codes, redirects, gzip, truncated bodies, slow-loris.
4. Agent-loop test with a stub transport: tool call → result → final answer;
   unknown tool; malformed arguments; timeout path.

**Test that proves it:** fuzzers find nothing in the budget; each new test fails
against the pre-fix code.

---

## A8 — Release gaps (S3)

Repository, MIT license, versioning and CI are now delivered: the public repo is
`github.com/0xFlan/ps5-harness`, and both Ubuntu and macOS jobs run the full
test suite. The remaining release gap is the CA-bundle/clock policy for console
TLS and a changelog. See P0 in `docs/PLAN.md`.

**TLS policy to write down (not just implement):** ship a CA bundle rather than
trusting a system store that may not exist; check the clock before the first
TLS request and fail with an actionable message; never disable verification,
including behind a "debug" flag.

---

## Fix verification (P1 close-out)

Run on the host, macOS, Apple clang, `-Wall -Wextra`:

- `make test` — core + hardening suites pass
- `make test CXXFLAGS="... -fsanitize=address,undefined ..."` — passes
- `make fuzz-run` — 20,000 iterations each, no findings
- `--call ep_archive_search` against archive.org — still works with the URL
  policy enforcing https, DNS re-check and address pinning

Not verified: anything requiring a PS5, and a real model round trip.

## Round-trip findings (P7)

Building the provider round-trip test exposed two more defects. Both are fixed,
with the tests that caught them now in `make test`.

| # | Issue | S | Status |
|---|---|---|---|
| A9 | The tool deadline did not actually bound a call | S2 | **Closed** |
| A10 | CLI flags were overwritten by the config file | S2 | **Closed** |

### A9 — deadline blocked until the handler finished (S2)

**Where:** `src/core.cpp` `run_with_deadline`, which used `std::async`.

**Why it was wrong:** the future `std::async` returns blocks in its destructor.
On timeout the function returned an error, but the future's destruction still
joined the worker — so a hung plugin wedged the loop for as long as it hung,
and reported a timeout only after the hang ended. The deadline was cosmetic.

**Fix:** a detached thread plus a shared `std::promise`/`std::future`. On expiry
the function returns immediately; the abandoned worker owns its own copy of the
handler and arguments and writes into a promise nobody is waiting for, which it
handles. **Proof:** `test_deadline_returns_without_blocking` asserts the tool
result reads `timed out after 80 ms` and the whole run finishes in under 400 ms
while the handler sleeps 600 ms.

### A10 — `--base-url` silently lost to the config (S2)

**Where:** `src/main.cpp` argument parsing.

**Why it was wrong:** flags were applied during argv parsing, then the config
file overwrote them. `--base-url` therefore did nothing whenever a config was
also passed — which is exactly how a test (or a user pointing at a stub, a
proxy, or a local gateway) ends up talking to the default endpoint instead. The
round-trip test caught it by reaching the real `api.openai.com` and returning
401.

**Fix:** track whether `--model` / `--base-url` were given and skip those config
assignments. **Proof:** `test_cli_round_trip` reaches the stub, and the stub's
recorded request count is exactly two.

## Re-audit triggers

Run this audit again after: any new tool that touches the filesystem or
network; any ABI change; any new trust tier; before every release; and whenever
a manifest or plugin from outside this repo is loaded during testing.
