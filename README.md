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
        │ fs_list/read/write/edit,  │               │
        │ http_get, download, skill │               │
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
make test           # policy-surface, core, resources, runtime, hardening, agents, skills, package and e2e
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
| Skills are read-only instruction packs; resource paths are canonicalised and confined to the skill directory | `src/skills.cpp` |
| Every tool result scrubbed of known secret values before it enters the message array | `src/secrets.cpp`, `src/agent.cpp` |
| Manifests are https-only, host-policied, private/loopback ranges denied, DNS re-checked and pinned | `src/policy.cpp`, `src/http.cpp` |
| Buffered responses capped at 4 MB; downloads stream to disk with Range resume and sha256 | `src/http.cpp` |
| Every tool call has a deadline (30 s default, per-tool override) | `src/core.cpp` |
| Plugin results go into a **host-owned** buffer — nothing is freed across the ABI | `include/ps5h/plugin_api.h` |
| The model endpoint is policy-checked too — the API key never travels over cleartext or to a private address | `src/agent.cpp` |

`fs_write`, `fs_edit` and `download` refuse any path outside `allowed_roots`.
`skill` refuses a name or resource outside the skills directory. Nothing
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

## Local agents

A local agent is a JSON profile on the console, not a compiled plugin. Profiles
are read at startup, so editing one on the PS5 takes effect on the next run with
no rebuild.

Default profile directory: `/data/homebrew` on the console, `agents` on the
host. `agents_dir` in the config sets another default, and `--agents <dir>`
overrides both. The file name is the agent name: `<dir>/<name>.json`.

```json
{
  "description": "Local archivist",
  "system_prompt": "You are the local archivist. Read before answering.",
  "model": "gpt-4o-mini",
  "temperature": 0.2,
  "max_tokens": 1024,
  "max_steps": 8
}
```

| Field | Meaning |
|---|---|
| `description` | One-line label shown by `--list-agents` |
| `system_prompt` | Inline system prompt |
| `system_prompt_file` | Prompt file relative to the profile directory (alternative to `system_prompt`) |
| `model` | Model id override |
| `temperature` | 0–2 |
| `max_tokens` | 0–32768; 0 omits the field |
| `max_steps` | 1–64 tool steps |

`system_prompt_file` is canonicalised and must stay inside the profile
directory; both prompt forms are capped at 64 KiB. A profile cannot set
`allowed_roots`, `read_roots`, `url_policy`, `secret_env`, `endpoints_dir`,
`plugins_dir`, `skills_dir`, `api_key_env` or `delegation_max_depth` — those
stay in the main config, so a profile cannot widen its own permissions.
Explicit `--model` and `--max-steps` flags win over the profile.

### Delegating to another profile

Any run exposing the `delegate` tool can hand a subtask to another local
profile:

```json
{"profile":"archivist","task":"list /data/homebrew and summarise it"}
```

The child is a normal agent run. It uses the profile prompt, model,
temperature, `max_tokens` and `max_steps`, and it receives a copy of the
parent's tool registry. That copy keeps the parent's `PathGuard`, `UrlPolicy`,
secrets and endpoint/plugin tools, so delegation cannot widen filesystem,
network or credential access. A profile that names `allowed_roots`,
`read_roots`, `url_policy`, `secret_env`, `endpoints_dir`, `plugins_dir`,
`skills_dir`, `api_key_env` or `delegation_max_depth` is rejected before the
child starts.

`delegation_max_depth` in the config (default 2; `--max-depth` overrides, and 0
disables delegation) limits nesting. The child reserves steps and tokens from
the parent's `max_steps`/`max_tokens` pool; unused steps and reported tokens
are refunded, while a provider that omits usage leaves its full token
reservation charged. An over-sized child request is refused, not clamped.
Explicit `--model` and `--max-steps` flags still win over the child profile.

The run log names both sides and the accounting:

```
[harness] delegate -> profile=archivist depth=1 task=list /data/homebrew
[harness] delegate answer from archivist: ...
[harness] delegate <- profile=archivist ok=true steps=2 tokens=123 remaining=4/389
```

`--call delegate --args '{"profile":"archivist","task":"..."}'` runs one
delegation without a parent model turn and prints a JSON result containing
`ok`, `profile`, `depth`, `steps`, `tokens` and the remaining budget. The
child's answer is returned to the parent as the tool result and is also logged;
it is not printed as a separate final answer.

```bash
./build/harness --agents examples/agents --list-agents
./build/harness --agents examples/agents --agent archivist --agent-info
./build/harness --config config.example.json --agents examples/agents \
  --agent archivist "what is in /data/homebrew?"
```

`--agent-info` resolves and prints the profile without calling a model, which is
useful on the console where no key is staged. On the PS5:

```bash
# copy archivist.json (and any prompt file) to /data/homebrew first
printf 'file:/data/harness.elf?args=--config%%20/data/ps5-harness.json%%20--list-agents\n' \
  | nc -w 30 <console-ip> 9021
printf 'file:/data/harness.elf?args=--config%%20/data/ps5-harness.json%%20--agent%%20archivist%%20--agent-info\n' \
  | nc -w 30 <console-ip> 9021
```

## Editing local files

Every run gets `fs_write` and `fs_edit`, and every delegated child receives the
same two tools through the inherited registry. Both write only under
`allowed_roots`, after the path is canonicalised and checked against the
secret-name rules:

- `fs_write(path, content)` creates or replaces a whole file. Use it for a new
  file or when the whole content genuinely changes.
- `fs_edit(path, old, new)` replaces exact text. The match must be unique unless
  `replace_all` is true, so a stale or ambiguous edit is refused instead of
  silently changing the wrong place. The file is read first, capped at 8 MiB,
  and the new content is written beside it and renamed into place.

Reading a file before editing it is part of the default prompt. A refusal names
the reason: outside the roots, denied path, text not found, or text appears more
than once.

## Skills

A skill is a folder of local instructions, not code and not a new permission.
Put it under the skills root:

```
<skills_dir>/<name>/SKILL.md
```

Default skills directory: `/data/homebrew/skills` on the console, `skills` on
the host. `skills_dir` in the config sets another default, and `--skills <dir>`
overrides both. `SKILL.md` may start with simple frontmatter:

```markdown
---
name: console-notes
description: Create, append to and review notes on the console.
---

# Console notes

1. Read the notes file before changing it.
2. Use fs_edit for a targeted append, or fs_write to create it.
3. Report the exact path from the tool result.
```

`name` must match the directory; `description` is what `skill` lists. The file
is capped at 256 KiB. The tool lists packs when `name` is omitted, loads one by
name, or reads a bundled file with `resource`:

```json
{"name":"console-notes"}
{"name":"file-audit","resource":"report-template.md"}
```

```bash
./build/harness --skills examples/skills --call skill --args '{}'
./build/harness --skills examples/skills --call skill \
  --args '{"name":"file-audit","resource":"report-template.md"}'
```

Resource paths are canonicalised, must stay inside that skill's folder, and are
denied by the same secret-name rules as `fs_read`; a symlink that leaves the
folder is refused. Resource output is clipped to the runtime tool-output budget.

A skill is guidance the model follows with the tools it already has. It cannot
set `allowed_roots`, `read_roots`, `url_policy`, `secret_env`, `endpoints_dir`,
`plugins_dir`, `skills_dir`, `api_key_env` or `delegation_max_depth`, and
loading one registers no tool and changes no guard. The directory is read on
every launch, so staging a pack on the console needs no rebuild.

`examples/skills/` has two working packs: `console-notes` and `file-audit`
(with a bundled `report-template.md`).

## Building for the PS5

The real agent, not the bring-up probe. `make -C platform/ps5` cross-compiles
every source in `src/` plus `platform/ps5/console_compat.cpp` twice — once as
PIE for elfldr, once as a fixed-address ET_EXEC for `make_fself.py` — and emits
one installable title folder:

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

`harness.elf` is **ET_DYN**: elfldr only spawns PIE payloads, and an ET_EXEC
image fails at spawn. `eboot.bin` is built from a second, non-PIE link because
`make_fself.py` rejects ET_DYN. `ps5_package_test` asserts the loader form.

At startup the agent probes online CPUs, visible memory, rlimits and every
mount, then derives its runtime budget: one tool worker per usable CPU, buffered
responses from a sixteenth of visible memory, and download capacity from the
destination filesystem minus a reserve. `tests/runtime_test.cpp` and the e2e
suite fail if those stop following the probe. `harness --resources` shows the
same numbers on the host.

`make -C platform/ps5 bringup` still builds the old diagnostic probe separately;
it is not the package artifact. `platform/ps5/BUILD.md` records the SDK and
PacBrew pins, artifact hashes, the shims and the console commands.

### Rest mode

**No. The harness cannot run in rest mode, and this build has no path to make it
do so.** It is a one-shot userland payload, not a service:

- `platform/ps5/Makefile:89` builds `harness.elf` as an ET_DYN payload
  "Loadable by elfldr on port 9021: ... arguments supplied by a file: URI."
- `platform/ps5/BUILD.md:197` and `docs/CONSOLE-REPORT.md:36` document the only
  runnable launch path: upload the ELF, then send elfldr a `file:` URI.
- `src/main.cpp:539` is the process entry. Every path returns: `--call` at
  `src/main.cpp:955`, `--tools` at `src/main.cpp:960`, a missing prompt at
  `src/main.cpp:966`, and a normal model run at `src/main.cpp:974`. There is no
  daemon loop after `main`.
- The harness owns no listener. `src/` has no `bind(`, `listen(` or `accept(`;
  the only ones in the tree are test stubs (`tests/hardening_test.cpp:63`,
  `tests/e2e_agent_test.cpp:119`). `src/http.cpp:154` is an outbound libcurl
  client, and `docs/SECURITY.md:27` records "no listening service by default".
- Ports 9021 (elfldr) and 8888 (web file manager) are separate userland
  processes, not servers started by the harness (`docs/CONSOLE-REPORT.md:13`).
  Suspend stops the console's userland, so those processes and the harness are
  not there to answer. Nothing in this repo asks the OS to keep them alive.

A suspended console therefore has no `harness` process to receive a tool call,
load a skill, edit a file or make a model turn. The only workable path for work
while the console sleeps is to queue jobs on an always-on host and, after the
console wakes, launch `harness.elf` and run the queue against it. There is no
wake-on-LAN, scheduler or background-service path here, and installing
`eboot.bin` as a title does not add one. This is also why verification kept the
console awake; waking it needs a human at the machine.

Remaining:

1. Install `eboot.bin` as a title and check the fake-SELF `--paid`/`--ptype`
   values and the SFO key set against your loader. A title launch supplies no
   argv or environment, so the runnable path is currently the `file:` URI on
   elfldr with `--config`/`--prompt`.
2. Record the firmware and exploit/loader versions, then repeat the run on the
   firmware you intend to support.
3. Test USB writes and an app-sandbox write; only `/data`, `/data/homebrew` and
   `/user` were written from the payload path.

## Status

Verified on the host:

- Clean build, `-Wall -Wextra`, no warnings
- `make test`: policy-surface, core, resources, runtime (CPU/fd/memory/free
  space budget, parallel executor), hardening (paths, redaction, sha256,
  streaming download with resume/verify, body cap, URL policy, deadlines),
  agent-profile and skill tests, PS5 package validation and end-to-end provider
  suites
- Suite passes under `-fsanitize=address,undefined`
- 20,000-iteration fuzz runs on the JSON parser and the manifest compiler, ASan
  clean
- Plugin ABI v2: an out-of-tree `.so` loads, registers and runs
- **Local agent profiles**: schema/path-confinement tests pass; the end-to-end
  CLI test proves a profile's system prompt, model, temperature and token cap
  reach the provider request, and `--list-agents`/`--agent-info` work without a
  model call
- **Delegation**: a parent stub conversation calls `delegate`; the child profile
  runs with the parent's tools and policy, a denied read outside the parent root
  proves the inherited guard, and the result returns to the parent. The suite
  also covers max-depth refusal, exact step/token refund accounting, explicit
  `--model`/`--max-steps` precedence, unknown profiles, self-delegation,
  policy-key rejection and budget exhaustion
- **Skills and targeted edits**: `skills_test` covers frontmatter, listing,
  bundled resources, traversal, symlink and secret-name refusal; the e2e CLI
  test lists and loads a skill, reads its resource, edits one unique match,
  refuses an ambiguous match, applies `replace_all`, refuses a write outside
  the roots, and asserts that a delegated child's tool schema still carries
  `skill` and `fs_edit`
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
  plus the console glue, links PacBrew libcurl/OpenSSL, and emits a PIE
  `harness.elf` for elfldr plus a fake-SELF `eboot.bin` from a separate non-PIE
  link (SDK v0.45, LLVM 23.1.4 on macOS)
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

Verified on the console (2026-11-18; core checks `0.1.0-fd5d0ae`, local agents
`0.1.0-4d7499f`, delegation `0.1.0-c0f76b5`, skills and edits `0.1.0-653d046`):

- **Boot and resources** — `harness.elf` starts under elfldr, writes
  `/data/ps5-harness-agent.log` and exits cleanly. The log records 16 online
  CPUs, 2185 MiB visible memory, `nofile 13952/13952`, 342 GiB free on `/data`,
  and `workers=16 buffered=136 MiB file-read=4 MiB`.
- **TLS** — `https://example.com/` returned 200 through the agent's static
  libcurl; `https://expired.badssl.com/` failed with a peer-certificate error,
  so verification was on.
- **`dlopen`** — the example plugin built with the SDK header loaded from
  `/data`, registered `hello_echo` and returned a result.
- **Writes** — `/data`, `/data/homebrew` and `/user` all wrote and read back.
- **Local agents** — on `0.1.0-4d7499f`, `--list-agents` read the archivist and
  maintainer profiles staged in `/data/homebrew`; `--agent-info` resolved the
  inline archivist prompt and the maintainer's `system_prompt_file`; overwriting
  `archivist.json` on the console changed the next `--list-agents` output to v2
  with no rebuild.
- **Delegation** — on `0.1.0-c0f76b5`, the log recorded the new build,
  `--tools` listed `delegate`, both profile commands still resolved (including
  `delegation_max_depth=2`), and `--call delegate` with a missing profile failed
  closed and logged `delegate ! ... cannot load agent`. No provider key was
  staged, so a delegated model turn was not run on hardware; the host e2e suite
  covers that path.
- **Skills and targeted edits** — on `0.1.0-653d046`, `--tools` listed 9 tools
  including `skill` and `fs_edit`; two packs staged under
  `/data/homebrew/skills` listed and loaded, and a bundled resource returned its
  contents. `../../etc/hosts` as a skill resource and an `fs_edit` on
  `/etc/hosts` were both refused. `fs_edit` changed one occurrence, a read-back
  returned `alpha new omega`, and a two-match edit was refused until
  `replace_all` was set. `--agent-info` printed
  `skills_dir=/data/homebrew/skills`.
- **Resume** — a 10 MiB transfer seeded with a 1 MiB `.part` was served as
  Range (`206`), finished at exactly 10,485,760 bytes and matched its sha256.
- **1 GB transfer** — on `0.1.0-fd5d0ae`, a 1 GiB OVH transfer seeded with a
  1,000 MiB prefix resumed with Range (`206`), reached exactly 1,073,741,824
  bytes, matched its expected sha256 in the agent, and renamed into place; no
  `.part` remained.
- **Free-space cap** — a 10 MB request on the 2 MiB `/mnt` tmpfs stopped after
  1.71 MB with `download exceeds the configured byte limit`.

`docs/CONSOLE-REPORT.md` has the transcript, both defects the run exposed (the
ET_EXEC spawn failure and a 64 KiB stack buffer in the digest path), and the
local-agent run.

Not verified or not built:

- **Rest mode is unsupported.** No payload runs while the console is in rest
  mode; this was not changed by this build, and it was not tested by suspending
  the console.
- **The title path is uninstalled.** `eboot.bin` was not installed or launched
  from the shell; the fake SELF, SFO key set and `--paid`/`--ptype` values still
  need a loader. A title launch also supplies no argv or environment, so the
  `file:` URI remains the runnable route.
- **Firmware and exploit versions are unrecorded.** No shell was available to
  query them.
- **USB and app-sandbox writes are untested.** No USB device was present and
  there was no title sandbox.
- **Peak memory during a 1 GB transfer is unmeasured.** The path streams in
  fixed chunks and the agent hashed the full 1 GiB file in 8 KiB chunks, but no
  RSS figure was captured on the console.
- **No model turn has run on hardware.** No provider key was available to the
  loader; the capability checks use `--call` over the same code paths.
- **CI has not executed.** The workflow is committed; no remote run yet.
- **No hosted provider has been used.** The round trip is verified against a
  local stub with the real client, policy, dispatch, deadline and redaction; a
  real account, real model and real tool-choice behaviour are untested.
- **No on-console UI.** CLI and `--call` only.
- **No elevation helper**, so writes reach only what the process can already
  open.

## Roadmap

1. First console boot, TLS and `dlopen` proofs (P2–P3).
2. A launch path that supplies config, prompt and the model key to the title.
3. Manifest registry with an index and trust tiers (P5).
4. `--serve` mode: LAN HTTP surface so a phone drives the console (P6).
5. Elevation helper for `/data/homebrew` writes (P4).
