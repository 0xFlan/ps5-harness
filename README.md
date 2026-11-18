# ps5-harness

An agent harness for a jailbroken PS5, in the shape of a desktop harness (a
Hermes-style loop over an OpenAI-compatible model) but with a native console
tool surface and two ways for other people to extend it.

The loop is the boring part. The point is the extension contract:

- **Endpoint manifests** — add an HTTP API with a JSON file, no compiled code.
- **Plugins** — a shared object exporting a stable C ABI, for anything a
  manifest can't express (pagination, auth flows, binary downloads).

```
        ┌───────────────────────────────────────────┐
        │ agent loop (src/agent.cpp)                │
        │  messages → model → tool_calls → results   │
        │  redaction + per-tool deadline            │
        └───────────────┬───────────────────────────┘
                        │ ToolRegistry
        ┌───────────────┴───────────┬───────────────┐
        │ builtin tools (src/main)  │               │
        │ fs_list/read/write,       │               │
        │ http_get, download        │               │
        ├───────────────────────────┤               │
        │ endpoint manifests        │  plugins      │
        │ (src/endpoints.cpp)       │ (src/plugins) │
        │ JSON → tools, no code     │ dlopen + C ABI│
        └───────────────────────────┴───────────────┘
```

## Build and run (host)

The core has no PS5 dependencies, so it builds and tests on macOS/Linux:

```bash
make                # build/harness
make test           # policy-surface, core, resources, runtime, hardening, package and e2e
make fuzz-run       # ASan/UBSan fuzz driver, 20k iterations each
make plugin-example # build the example plugin
make tools          # list built-in + manifest tools
./build/harness --resources   # CPUs, memory, rlimits and mount space
```

Talk to a model (key comes from the environment, never the config file):

```bash
export PS5H_API_KEY=...
./build/harness --config config.example.json "what is in examples/endpoints?"
```

Explicit flags win over the config file (`--base-url` and `--model` are not
overwritten by `model.*` in the config).

Test one tool without spending tokens:

```bash
./build/harness --endpoints examples/endpoints \
  --call ep_archive_search --args '{"query":"collection:classicpcgames","rows":"2"}'
```

## Security model

These are enforced in code, not by convention. `docs/SECURITY.md` is the full
model; `docs/AUDIT.md` records the defects that produced each rule.

| Rule | Where |
|---|---|
| Paths canonicalised (`realpath`), symlinks resolved, `..` rejected, boundary checked with a separator | `src/paths.cpp` |
| Reads and writes confined to configured roots; config, credentials and state denied by name | `src/paths.cpp`, `src/main.cpp` |
| Every tool result scrubbed of known secret values before it enters the message array | `src/secrets.cpp`, `src/agent.cpp` |
| Manifests are https-only, host-policied, private/loopback ranges denied, DNS re-checked and pinned | `src/policy.cpp`, `src/http.cpp` |
| Buffered responses capped at 4 MB; downloads stream to disk with Range resume and sha256 | `src/http.cpp` |
| Every tool call has a deadline (30 s default, per-tool override) | `src/core.cpp` |
| Plugin results go into a **host-owned** buffer — nothing is freed across the ABI | `include/ps5h/plugin_api.h` |
| The model endpoint is policy-checked too — the API key never travels over cleartext or to a private address | `src/agent.cpp` |

`fs_write` and `download` refuse any path outside `allowed_roots`. Nothing
disables TLS verification; there is no debug flag that does.

### Testing the provider round trip

The end-to-end test needs a cleartext loopback stub, which the production policy
correctly refuses. The escape hatch is **compile-time only**: `UrlPolicy`'s
`test_allow_loopback_cleartext` field and `--test-allow-loopback-cleartext` flag
exist only in builds defining `PS5H_TEST_HOOKS` (the test targets), they permit
loopback cleartext and nothing else, and no config file can reach them.
`tests/policy_surface_test.cpp` fails to compile if the field ever appears in a
production build, and the e2e suite proves the shipped binary rejects both the
cleartext endpoint and the flag.

## Adding an API: endpoint manifests

Drop a `.json` file in the endpoints directory. Every operation becomes a tool
named `ep_<id>_<operation>`:

```json
{
  "id": "archive",
  "description": "Internet Archive",
  "baseUrl": "https://archive.org",
  "auth": { "type": "none" },
  "headers": { "Accept": "application/json" },
  "operations": {
    "search": {
      "method": "GET",
      "path": "/advancedsearch.php",
      "query": { "q": "{{query}}", "rows": "{{rows}}", "output": "json" },
      "pick": "response.docs",
      "description": "Search the catalogue"
    }
  }
}
```

| Field | Meaning |
|---|---|
| `id` | Tool name segment; sanitised to `[a-z0-9_-]` |
| `baseUrl` | Must be `https`; checked against the URL policy at load and at call |
| `auth` | `none`, `bearer` (env var), `header` (env var + header), `query` (env var + param) |
| `headers` | Sent on every operation |
| `operations.<name>.method` | `GET` (default), `POST`, … |
| `operations.<name>.query` | Static or `{{placeholder}}` values, URL-encoded |
| `operations.<name>.body` | `POST` body; strings may contain `{{placeholder}}` |
| `operations.<name>.pick` | Dotted path applied to a JSON response before it reaches the model |
| `operations.<name>.timeout_seconds` | Per-tool deadline, default 30 |

Every distinct `{{placeholder}}` becomes a required string parameter. Auth is
read from the environment at call time; a missing credential returns an error
instead of sending an unauthenticated request. Redirects are disabled, so a
manifest cannot bounce to another host.

## Adding behaviour: plugins

`include/ps5h/plugin_api.h` is the contract (ABI v2). A plugin exports
`ps5h_plugin_init` and `ps5h_plugin_id` and gets:

- `register_tool(name, description, JSON Schema, handler, user)`
- a handler that receives arguments and writes its result with `emit(...)`
- `http_request(request_json)` — the host does TLS; plugins never link OpenSSL
- `get_env(name)` — credentials, nothing stored in the plugin binary
- `log(level, message)`, `free_string`

`examples/plugins/hello/hello.c` is a working example. The ABI is append-only
and versioned; check `host->abi_version` before using newer members.

**Trust model.** A manifest is data. A plugin is arbitrary code in the harness
process with network access and the harness's filesystem permissions. Only load
plugin directories you would run as a program.

## Building for the PS5

The real agent, not the bring-up probe. `make -C platform/ps5` cross-compiles
every source in `src/` plus `platform/ps5/console_compat.cpp` to an x86-64
FreeBSD ELF and emits one installable title folder:

```
PHAR00001/
  eboot.bin            fake SELF built from harness.elf
  harness.elf          the real agent ELF, for elfldr
  sce_sys/param.sfo    generated SFO binary (real header, entry table, keys)
  sce_sys/icon0.png    generated 512x512 RGBA icon
  sce_sys/param.json   metadata for installers that stage JSON instead
```

The agent links PacBrew's static libcurl 8.18.0 and OpenSSL 3.5.2 (plus zlib,
zstd and libpsl) for the payload SDK's libc. The target build adds the console's
CA bundle, the `SO_NBIO` socket mode and an `fcntl` wrapper for sandboxed
sockets. The SDK libc already provides the resolver and libc symbols native
titles had to stub, so those are not redefined here.

At startup the agent probes online CPUs, visible memory, rlimits and every
mount, then derives its runtime budget: one tool worker per usable CPU, buffered
responses from a sixteenth of visible memory, and download capacity from the
destination filesystem minus a reserve. `tests/runtime_test.cpp` and the e2e
suite fail if those stop following the probe. `harness --resources` shows the
same numbers on the host.

`make -C platform/ps5 bringup` still builds the old diagnostic probe separately;
it is not the package artifact. `platform/ps5/BUILD.md` records the SDK and
PacBrew pins, artifact hashes, the shims and the console commands.

Remaining:

1. Boot the agent on a console and record the log. A title launch supplies no
   argv, so the packaged `eboot.bin` currently prints usage; a runnable session
   needs a loader that preserves `--config`/`--prompt` and the model key.
2. Verify DNS, the CA list and the clock before claiming TLS (P3).
3. Confirm `dlopen` of a plugin `.so` and elevated writes on your firmware (P3).
4. Check the fake-SELF `--paid`/`--ptype` values and the SFO key set against
   your loader, using a working homebrew title as the reference.

## Status

Verified on the host:

- Clean build, `-Wall -Wextra`, no warnings
- `make test`: policy-surface, core, resources, runtime (CPU/fd/memory/free
  space budget, parallel executor), hardening (paths, redaction, sha256,
  streaming download with resume/verify, body cap, URL policy, deadlines),
  PS5 package validation and end-to-end provider suites
- Suite passes under `-fsanitize=address,undefined`
- 20,000-iteration fuzz runs on the JSON parser and the manifest compiler, ASan
  clean
- Plugin ABI v2: an out-of-tree `.so` loads, registers and runs
- Live HTTPS call through a manifest to `archive.org/advancedsearch.php`
- **Full provider round trip** through the test build of the CLI
  (`build/harness_e2e`, same sources plus the test hook) against a local
  OpenAI-compatible stub: user turn → assistant `tool_calls` → tool result with
  the matching `tool_call_id` → second provider call that resends the history →
  final assistant message. Malformed JSON and a truncated response end the turn
  with a clean error; an unknown tool becomes a tool-level error the loop
  recovers from; the tool deadline returns without waiting for the abandoned
  handler
- **Real agent cross-build**: `make -C platform/ps5` compiles all `src/*.cpp`
  plus the console glue, links PacBrew libcurl/OpenSSL, and packs a fake-SELF
  `eboot.bin` from `harness.elf` (SDK v0.45, LLVM 23.1.4 on macOS)
- **PS5 package validation**: `ps5_package_test` parses the SFO and icon from
  disk, checks the fSELF magic and the agent ELF header, and passes on the
  package above
- **Runtime scaling**: `runtime_test` fails if worker count stops tracking
  online CPUs or the fd limit, if the buffered response stops scaling with
  memory, or if download capacity ignores free space/`RLIMIT_FSIZE`; the e2e
  suite proves parallel tool calls use the budgeted executor
- **Resource probe on the host**: `resources_test` asserts CPU, page size,
  physical memory, mount enumeration and report formatting; `harness
  --resources` prints this Mac's profile

Not verified or not built:

- **Nothing has run on a PS5.** The package contains the real agent, but it has
  never been launched or installed; P2's boot criterion, TLS, DNS, `dlopen` and
  elevated writes are all console-only and unclaimed.
- **The console's resource envelope is unknown.** CPU count, visible memory,
  rlimits and mount points are pending the first agent run.
- **A title launch passes no arguments.** The `eboot.bin` agent has no config,
  prompt or key in that path yet; the ELF path with a loader that preserves argv
  is the current way to run a turn.
- **CI has not executed.** The workflow is committed; no remote run yet.
- **No hosted provider has been used.** The round trip is verified against a
  local stub with the real client, policy, dispatch, deadline and redaction; a
  real account, real model and real tool-choice behaviour are untested.
- **No on-console UI.** CLI and `--call` only.
- **No elevation helper**, so writes reach only what the process can already
  open.
- **Download resume** is implemented and unit-tested against a local fixture,
  but a 1 GB interrupted transfer on hardware is unverified.

## Roadmap

1. First console boot, TLS and `dlopen` proofs (P2–P3).
2. A launch path that supplies config, prompt and the model key to the title.
3. Manifest registry with an index and trust tiers (P5).
4. `--serve` mode: LAN HTTP surface so a phone drives the console (P6).
5. Elevation helper for `/data/homebrew` writes (P4).
