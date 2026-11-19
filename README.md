# ps5-harness

An agent harness for a jailbroken PS5, in the shape of a desktop harness (a
Hermes-style loop over an OpenAI-compatible model) but with a native console
tool surface and two ways for other people to extend it.

The loop is the boring part. The point is the extension contract:

- **Endpoint manifests** — add an HTTP API with a JSON file, no compiled code.
- **Plugins** — a shared object exporting a stable C ABI, for anything a
  manifest can't express (pagination, auth flows, binary downloads).

The harness also carries a local recon surface (`env_info`, `fs_tree`,
`file_info`, `fs_strings`), guarded write and process-inspection tools, a
read-only `--plan` mode, bounded `delegate`/`swarm` agents and JSON missions.
Six clean-room skill packs are compiled in, so a fresh console has
reverse-engineering and local-architecture methodology without staging
anything first. `--serve` adds an optional LAN browser workspace with a chat
transcript, live tool/activity panel, tool catalogue, persistent sessions and
model-assisted context compaction.

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
        │ fs_mkdir/move/delete,     │               │
        │ fs_search, hexdump,       │               │
        │ http_get, download, skill │               │
        │ env_info, fs_tree,        │               │
        │ file_info, fs_strings,    │               │
        │ sysctl, delegate, swarm   │               │
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
make test           # policy, core, resources, runtime, hardening, agents, skills, recon, missions, session, tools, server, browser reconnect, package and e2e
make fuzz-run       # ASan/UBSan: JSON, manifest, recon and mission fuzzers, 20k iterations each
make sanitize-test  # recon and mission host tests under ASan/UBSan
make leaks-check    # macOS system leak check on the plain recon/mission fuzz drivers
make plugin-example # build the example plugin
make tools          # list built-in + manifest tools
./build/harness --resources   # CPUs, memory, rlimits and mount space
```

Start the browser workspace (the token is printed once; the page itself is
served without it so it can render the login prompt):

```bash
./build/harness --config config.example.json --serve 8787
# open http://<console-or-host-ip>:8787/?token=<printed-token>
```

`--serve-token <16+ chars>` fixes the token, `--session <path>` overrides the
session file (default: `<first write root>/ps5h-session.json`), and
`--context-chars <n>` sets the transcript budget (default 120000 characters,
minimum 4096). The UI is an opt-in mode; without `--serve` the binary still
does one turn and exits.

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
| Skills are read-only text packs; built-ins are compiled in and disk packs override by name; resource paths are canonicalised and confined to the skill directory | `src/skills.cpp`, `src/builtin_skills.cpp` |
| Plan mode removes every mutating tool from the registry, including endpoint and plugin tools | `src/main.cpp` |
| Recon tools are read-only, bounded, path-guarded and deny secret-named paths; `env_info` hides other titles' sandbox mounts | `src/recon.cpp` |
| Swarms and missions reuse the delegation depth, shared budget and child-registry rules | `src/delegation.cpp`, `src/mission.cpp` |
| Every tool result scrubbed of known secret values before it enters the message array | `src/secrets.cpp`, `src/agent.cpp` |
| Manifests are https-only, host-policied, private/loopback ranges denied, DNS re-checked and pinned | `src/policy.cpp`, `src/http.cpp` |
| Buffered responses capped at 4 MB; downloads stream to disk with Range resume and sha256 | `src/http.cpp` |
| Every tool call has a deadline (30 s default, per-tool override) | `src/core.cpp` |
| Plugin results go into a **host-owned** buffer — nothing is freed across the ABI | `include/ps5h/plugin_api.h` |
| The model endpoint is policy-checked too — the API key never travels over cleartext or to a private address | `src/agent.cpp` |
| Browser listener is off unless `--serve` is explicit; every `/api/*` call requires the bearer token; one turn runs at a time; no cookies, CORS or wildcard origins | `src/server.cpp` |
| Public discovery exposes only service/build/title/ports/addresses; the token, session and tool data stay authenticated. UDP discovery can be disabled with `--discovery-port 0` | `src/server.cpp`, `tools/ps5h-discover.py` |
| Session files stay under the write roots and are rejected if their names look secret-bearing | `src/main.cpp`, `src/paths.cpp` |

`fs_write`, `fs_edit` and `download` refuse any path outside `allowed_roots`.
`skill` refuses a name or resource outside the skills directory. Nothing
disables TLS verification; there is no debug flag that does.

Built-in skills are source text compiled into the binary, not downloaded
content. The `astrik`, `artex`, `strix` and `cyber-strike-ai` packs are
clean-room methodology cards, not the upstream projects: they contain no
third-party code, binaries, scripts or network endpoints, and they cannot
certify an external ARTEX tree. Treat those names as a description of the
workflow, not as provenance for code that is not in this repository.

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

## Local reconnaissance

Four read-only recon tools are always registered. They are the harness's own
answer to "what machine am I on, and what is around me?"

| Tool | What it reports |
|---|---|
| `env_info` | Build/title, platform, cwd, read/write roots, skills dir, CPUs, memory, limits, runtime budget and system mounts |
| `fs_tree` | Bounded directory tree under the read roots, depth 1–8 (default 3), at most 500 entries; symlinks are reported, never followed; secret-named entries are marked `denied` |
| `file_info` | Size, mode, uid/gid, mtime, sha256 (files up to 64 MiB), magic, and ELF/Mach-O/PE headers with class, machine, type and entry point |
| `fs_strings` | Up to 200 printable ASCII strings from the first 16 MiB of a file, minimum length 4–64; it does not execute or decompile the file |

All four go through the configured read roots and the same secret-name rules as
`fs_read`. `fs_tree` does not follow a symlink out of a root, `file_info`
canonicalises before opening, and `env_info` filters other titles'
`/mnt/sandbox/...` mounts and caps the mount list. The recon surface itself does
not write or chmod; the separate power tools below do, under the write guard.

```bash
./build/harness --call env_info --args '{}'
./build/harness --call fs_tree --args '{"path":".","depth":2}'
./build/harness --call file_info --args '{"path":"build/harness"}'
./build/harness --call fs_strings --args '{"path":"build/harness","min_length":8}'
```

## Power tools and the browser workspace

The default registry is not read-only. It already contains `fs_write`,
`fs_edit`, `http_get`, `download`, endpoint/plugin tools and delegation. The
power surface adds guarded primitives so an investigation can create a working
tree, rename evidence, search it and inspect raw bytes without dropping to a
shell:

| Tool | What it does |
|---|---|
| `fs_mkdir` | Creates one directory under the write roots; parent must exist |
| `fs_move` | Renames/moves inside the write roots; refuses overwrite unless `overwrite=true`; never splits cross-device moves |
| `fs_delete` | Deletes one file or one empty directory; never recursive; symlinks are unlinked, not followed |
| `fs_search` | Literal text search with bounded recursion, files, bytes, depth and result count; returns `path:line:column` evidence |
| `hexdump` | Bounded hex + ASCII view of a file range for headers, tables and opaque structures |
| `sysctl` | Allows `kern.*`, `hw.*`, `vm.*`, `net.*`, `machdep.*` and `dev.*` reads; argument/env/credential/key/token/secret names are denied |

Every path still goes through the same canonicalisation and write guard as
`fs_write`; secret-named paths (including `.env`) stay denied. `--plan` removes
the mutating tools from the registry rather than relying on the prompt.

`--serve` is the workspace shell: a dark three-panel page served by the harness
itself. The left rail starts/reset sessions; the centre is a persistent chat with
user/assistant turns and a composer; the right inspector shows live tool activity
and the registered tool catalogue, plus context, message, compaction, token and
step counters. The page is dependency-free (no CDN or web fonts), works on a
phone on the same LAN, and stores the transcript locally. History is saved after
every turn, loaded on restart, and compacted only when the approximate transcript
budget is exceeded; the newest turns are kept verbatim.

### Input

The page is usable without a console-side keyboard or mouse, and without the
terminal. Input is handled by the browser that opens the page:

- **Physical keyboard and mouse** — native DOM input. Type in the composer,
  paste, select, click, scroll, use Tab/Shift+Tab and Ctrl/⌘K to return to the
  composer. Enter sends; Shift+Enter inserts a newline.
- **Controller** — the Gamepad API maps D-pad/left stick to spatial focus,
  Cross/A to activate, Circle/B to go back, Square/X to open the on-screen
  keyboard (or backspace while it is open), Triangle/Y to focus the composer,
  Options/Start to toggle the keyboard, Create/Select for input help, L1/R1 for
  Activity/Tools, triggers for scrolling, and L3/R3 or the touchpad button for
  the pointer.
- **On-screen keyboard** — a controller-navigable keyboard with letters, digits,
  punctuation, Shift, Backspace, Space and Enter. Controller focus rings move
  between keys; mouse/touch clicks work too. It targets the composer or the
  login token field, whichever was focused when opened.
- **Touchpad and pointer** — when the browser exposes Gamepad touch surfaces,
  touch position moves a virtual pointer, a short tap or the touchpad click
  activates the control under it. Browsers that do not expose touch coordinates
  fall back to the right stick and R3/L3, with the same pointer overlay.
- **Dictation** — the mic button starts the browser's Web Speech recognition
  (Chrome/Edge/Safari, where available), inserts final phrases at the cursor and
  shows the selected audio input. The browser's selected microphone is used;
  select a controller headset as the system/browser input if that is the mic you
  want. A browser without Web Speech can still use its own keyboard dictation
  key into the composer.

The page reads the device running the browser. A keyboard, mouse, controller or
headset paired only to the console is not forwarded to a remote browser by this
payload. Web Speech and microphone permissions require HTTPS or localhost. The
console now serves HTTPS directly on port 8790; the HTTP companion on 8789 and
the new HTTP listener on 8791 explain the requirement when the mic button is
pressed. See [HTTPS and microphone trust](#https-and-microphone-trust) for the
certificate and trust steps.

```bash
./build/harness --config config.example.json --serve 8787 \
  --session /data/ps5h-session.json

# Serve HTTP and HTTPS from the same console payload:
./build/harness --config /data/ps5h-mission.json --serve 8791 \
  --serve-tls 8790 --tls-cert /data/ps5h-cert.pem \
  --tls-key /data/ps5h-key.pem --serve-token <token> \
  --session /data/ps5h-mission-session-final.json
```

### HTTPS and microphone trust

The console serves the same token-gated workspace over both schemes. The current
deployment uses:

| Item | Value |
|---|---|
| HTTPS | `https://192.168.4.100:8790/` |
| Browser workspace | `0.1.0-tile` (home-screen tile and PS5 chat presentation) |
| Home tile | `/data/homebrew/HARN00001` (`HARN00001`) |
| HTTP companion | `http://192.168.4.100:8789/` (current `0.1.0-tile`, PID 107 at the 2026-11-18 17:06 recheck) |
| New HTTP | `http://192.168.4.100:8791/` (PID 103 at the 2026-11-18 17:06 recheck) |
| Certificate | `/data/ps5h-cert.pem` (public) |
| Private key | `/data/ps5h-key.pem` (keep on the console) |
| Certificate identity | `CN=PS5 Harness`, SAN `192.168.4.100`, `127.0.0.1`, `localhost` |
| SHA-256 fingerprint | `8C:80:7E:62:8F:B0:4B:CB:70:25:7C:6A:97:06:43:CD:96:63:24:FF:C7:CA:67:DE:79:32:49:9A:7E:BD:F7:B3` |

The staged certificate's SAN is tied to `192.168.4.100`. If the console gets a
new DHCP address, use the HTTP listener or regenerate the certificate with the
new address in its SAN; otherwise HTTPS will show a name-mismatch warning. The
discovery helper prints the HTTP origin for this reason.

The certificate is self-signed (and valid as a local root), so treat it as a
local CA and trust it on the client. Download `/data/ps5h-cert.pem` through the
console's web file manager, save it as `ps5h-cert.pem`, then import it into
Keychain Access and set **Always Trust**, or run:

```bash
security add-trusted-cert -r trustRoot \
  -k ~/Library/Keychains/login.keychain-db ps5h-cert.pem
```

On iOS/iPadOS, install the certificate profile, then enable it under
**Settings → General → About → Certificate Trust Settings**. Chrome and Edge use
the operating system trust store. After trusting it, open the HTTPS URL without
a certificate warning. Browsers still require the bearer token; TLS does not
replace authentication.

On the current test Mac the certificate **is imported** into
`~/Library/Keychains/login.keychain-db`, but the trust approval is still pending:
`security dump-trust-settings` reports no trust settings and
`security verify-cert` reports `CSSMERR_TP_NOT_TRUSTED`. macOS opened a
`SecurityAgent` authorization prompt that an automated session cannot approve.
Complete that prompt, or open the imported **PS5 Harness** certificate in
Keychain Access and set **Always Trust** for SSL. Until that user-only step is
done, the browser test below uses a certificate bypass and does **not** prove
warning-free trust.

If the certificate is not trusted, clicking through the warning still yields a
secure context and the microphone prompt, but that bypass is only for testing;
trust the certificate for normal use. The HTTP pages remain available and show
the HTTPS/localhost guidance instead of silently failing when the mic button is
pressed.

### Connection loss, restarts and address changes

The workspace keeps the last session in `--session` and retries the same origin
with a short request timeout. A **Console connection lost** banner lets
the user retry immediately or enter a new address/port; the token is carried to
the new origin and removed from the address bar after the page loads. See
[`docs/RECOVERY.md`](docs/RECOVERY.md) for the full support runbook.

The HTTP listener binds `0.0.0.0`, so a changed DHCP address does not require a
rebuild or source change. The serving port is whatever `--serve` chooses. A
primary listener with `--discovery-port 8799` also answers a UDP query and
publishes a public identity document at `/.well-known/ps5h`:

```bash
# Public document; it contains ports/addresses/build/title, never the token.
curl http://192.168.4.100:8791/.well-known/ps5h

# Find the console after its IP or port changed.
python3 tools/ps5h-discover.py
python3 tools/ps5h-discover.py --wait
python3 tools/ps5h-discover.py --subnet 192.168.4.0/24   # if UDP is blocked
```

The helper, server and browser-runtime tests are host-side only in this tree;
the running `0.1.0-tile` console process must be rebuilt and relaunched to
include the new endpoint and banner. Use a fixed `--serve-token` for a customer
setup; a token generated because the flag was omitted changes on every launch.

`make reconnect-test` drives the served page in headless Chrome against local
HTTP and HTTPS harnesses. It stops and restarts a harness, changes the origin
and port while preserving the token, checks that the transcript reloads, and
checks that a stale token produces the login/rejected-token state instead of a
dead-console state. It does not make rest mode recoverable.

This does **not** make the payload survive rest mode. Rest mode stops the
userland process; after wake, re-run the exploit if required and launch the
payload again. The saved session resumes and the home tile is re-registered on
that launch when auto-install is enabled and the tile was not removed. The
harness has no wake-on-LAN or background-service path.

## Plan mode, swarms and missions

### Read-only plan mode

`--plan` builds the registry without `fs_write`, `fs_edit`, `fs_mkdir`,
`fs_move`, `fs_delete`, `http_get`, `download`, endpoint tools or plugin tools,
and appends a read-only planning
instruction to the system prompt. The model can still inspect the machine and
load skills, but a call to a missing tool is an unknown-tool error and nothing
is created. Delegated children inherit the same read-only registry.

```bash
./build/harness --config config.example.json --plan \
  "map the local layout and propose the next checks"
./build/harness --plan --tools    # 12 tools: recon + read + search/hexdump/sysctl + skill + delegate/swarm
```

Plan mode is not a sandbox for a malicious plugin. It never loads endpoint or
plugin directories, and the ordinary process permissions still apply.

### Swarms

The `swarm` tool runs up to eight independent child runs in parallel, preserving
input order. Each lane is a normal delegated child: it gets the parent's
read/write roots, URL policy, secrets, endpoints, plugins and shared
step/token budget, and it can delegate further only within
`delegation_max_depth`. One failed lane does not cancel the others; the merged
result is `ok:false` if any lane failed.

```json
{"tasks":[
  {"profile":"archivist","task":"inspect /data binaries with file_info","max_steps":3},
  {"profile":"maintainer","task":"map configuration and mounts read-only","max_steps":3}
]}
```

The width limit is hard. Use lanes for orthogonal surfaces, not for repeating
the same question; the shared pool will refuse an over-sized reservation rather
than silently serialize or overspend it.

### Missions

A mission is a bounded JSON sequence of steps. Each step runs a profile at
depth 1 (so it may delegate one level further), or a swarm of lanes. The runner
stops at the first failed step and appends a clipped summary of prior answers
to later tasks, which gives a multi-step/multi-level run without an unbounded
agent loop. Budgets are per step and per lane, and the whole run still uses the
parent's `--max-steps`/`max_tokens` pool.

```json
{
  "name": "local-map",
  "steps": [
    {"id": "map", "profile": "archivist", "task": "map the read roots", "max_steps": 4},
    {"id": "lanes", "swarm": [
      {"profile": "archivist", "task": "identify binaries", "max_steps": 3},
      {"profile": "maintainer", "task": "map configuration", "max_steps": 3}
    ]}
  ]
}
```

```bash
./build/harness --config config.example.json --agents examples/agents \
  --mission examples/missions/local-map.json
```

The file is capped at 512 KiB, 32 steps, 8 lanes per step and 8 KiB per task.
There are no conditionals, loops or shell commands: a mission composes agent
turns, not arbitrary code. `examples/missions/local-map.json` is a working
starting point.

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

### Built-in packs

Six packs are compiled into the binary and always available, even when no
skills directory has been staged. A disk pack with the same name overrides the
embedded copy.

| Pack | Use |
|---|---|
| `reverse-engineer-anything` | Static-first triage, strings, hypotheses and evidence levels for an unfamiliar artifact; it states plainly that the harness has no disassembler or debugger |
| `local-architect` | Map mounts, executables, configuration, data flow and trust boundaries without writing |
| `astrik` | Condensed authorized local operator method: charter, cheap probes, L1–L4 evidence, chains and cleanup |
| `artex` | Clean-room ARTEX-style objective/surface/primitive/proof methodology; no upstream code or binaries |
| `strix` | Parallel recon lanes and evidence merge over a bounded swarm |
| `cyber-strike-ai` | Plan mode, mission JSON, budgets and stop conditions |

These are first-party text files in `src/builtin_skills.cpp`. The names are
compatibility shorthand for a workflow, not an audit or endorsement of any
upstream project, and the built-ins deliberately contain no executable content.
If you need a specific upstream ARTEX tree, audit and stage that tree yourself;
this repository does not ship it.

`examples/skills/` has two working disk packs: `console-notes` and
`file-audit` (with a bundled `report-template.md`).

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
  sce_sys/icon0.png    generated 512x512 PS5-style tile (white H, blue bridge)
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
do so.** It is a userland payload, not a background service:

- `platform/ps5/Makefile` builds `harness.elf` as an ET_DYN payload loadable by
  elfldr on port 9021; arguments still come from a `file:` URI.
- `src/main.cpp` is the process entry. `--call`, `--tools`, `--resources`, a
  normal prompt and `--plan` all return. Only `--serve` stays alive, and only
  because the user explicitly launched a LAN listener; it is not started by the
  title path or by rest mode.
- The listener is `src/server.cpp` (`bind`, `listen`, `accept`), disabled by
  default; every `/api/*` call needs the bearer token, while the page, tile icon
  and `/.well-known/ps5h` discovery document are public read-only surfaces.
  `src/http.cpp` is still the outbound libcurl client. `docs/SECURITY.md`
  records the listener rule and the token boundary.
- Ports 9021 (elfldr) and 8888 (web file manager) are separate userland
  processes, not servers started implicitly by the harness
  (`docs/CONSOLE-REPORT.md`, Environment table). Suspend stops the console's userland, so those
  processes and the harness are not there to answer. Nothing in this repo asks
  the OS to keep them alive.

A suspended console therefore has no `harness` process to receive a tool call,
load a skill, edit a file or make a model turn. The only workable path for work
while the console sleeps is to queue jobs on an always-on host and, after the
console wakes, launch `harness.elf` and run the queue against it. There is no
wake-on-LAN, scheduler or background-service path here, and installing
`eboot.bin` as a title does not add one. This is also why verification kept the
console awake; waking it needs a human at the machine. The reconnect banner and
discovery document described above make the post-wake relaunch easier to find
and resume; they do not keep the process alive or start it automatically. Use
[`docs/RECOVERY.md`](docs/RECOVERY.md) for the wake, re-jailbreak and relaunch
sequence.

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
  agent-profile, skill, recon, mission, session/compaction, power-tool and
  browser-server tests, a headless-Chrome reconnect test, PS5 package
  validation and end-to-end provider suites
- Suite passes under `-fsanitize=address,undefined`
- 20,000-iteration ASan/UBSan fuzz runs on the JSON parser, manifest compiler,
  recon header/string/tree surface and mission parser; the recon and mission
  host tests also pass sanitized. On macOS, where ASan has no LeakSanitizer,
  `make leaks-check` reports zero leaked bytes for both new fuzz drivers
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
  bundled resources, traversal, symlink and secret-name refusal, plus the
  embedded packs and disk-override precedence; the e2e CLI test lists and
  loads a skill, reads its resource, edits one unique match, refuses an
  ambiguous match, applies `replace_all`, refuses a write outside the roots,
  and asserts that a delegated child's tool schema still carries `skill` and
  `fs_edit`
- **Local recon, plan mode, swarms and missions**: `recon_test` covers ELF,
  Mach-O and PE identification, truncated and hostile headers (including PE
  offsets that wrap in 32-bit arithmetic), capped strings, bounded trees,
  symlink handling, secret-name denial, mount filtering and non-finite numeric
  arguments; `mission_test` covers valid/invalid missions, duplicate ids,
  width/step/budget caps, missing files and non-finite budgets. The e2e suite proves
  plan mode sends no mutating tool schema and cannot create a file, a mission
  runs a single step then a two-lane swarm with the first answer as context,
  and a nine-lane swarm is refused before any child starts
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
  (including the browser server, session, power tools and embedded UI) plus the
  console glue, links PacBrew libcurl/OpenSSL, and emits a PIE `harness.elf` for
  elfldr plus a fake-SELF `eboot.bin` from a separate non-PIE link (SDK v0.45,
  LLVM 23.1.4 on macOS). The SDK wrapper needs the host LLVM tools on `PATH`
  (for example `PATH=/opt/homebrew/opt/llvm/bin:$PATH`).
- **Browser workspace**: the token-auth server, static page, state/history
  routes, a full stub-backed turn, transcript persistence across restart and
  context compaction are covered by host tests and a live loopback smoke test
- **Connection recovery and discovery**: `server_test` checks the public
  `/.well-known/ps5h` document and the UDP responder without a token, and a
  loopback smoke run exercised `tools/ps5h-discover.py` through both UDP and
  the TCP fallback. The reconnect banner and its address hand-off are in the
  served page; they have not yet been exercised in a real browser.
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

> **Historical evidence.** The bullets below describe earlier builds and
> captures on 2026-11-18, not the current `0.1.0-tile` process. The initial run
> window was ≈09:18–12:05 local; the legacy 16:18 capture and the current
> 17:06 PID recheck and 17:14 service recheck are identified in their bullets.
> Exact per-build times were not recorded, and early launch PIDs were not
> recorded unless stated. The only current live evidence is the **Home-screen
> tile and PS5 chat** bullet; its values were rechecked at 17:14 PST.

Verified on the console (2026-11-18; core checks `0.1.0-fd5d0ae`, local agents
`0.1.0-4d7499f`, delegation `0.1.0-c0f76b5`, skills and edits `0.1.0-653d046`,
recon/plan/swarm/mission `0.1.0-e0b6af4`, parser-hardening rebuild
`0.1.0-e6b6205`, final LAN workspace `0.1.0-final`, browser input layer
`0.1.0-input`, HTTPS browser workspace `0.1.0-https`):

- **Home-screen tile and PS5 chat** — `0.1.0-tile` is the current browser
  workspace. At startup it writes `/data/homebrew/HARN00001` from the console's
  staged web shell (`/data/ps5h-web-shell.bin`, originally
  `/system_ex/app/NPXS40106/eboot.bin`), `sce_sys/param.json` and a generated
  512×512 `icon0.png`, all at `0777`. Payload processes run as
  uid 1 and cannot read `/system_ex` directly, so the exact console shell is
  staged at `/data/ps5h-web-shell.bin` and used as the copy source. At the
  17:14 PST recheck the current probe returned HTTP 200 with `status:0`;
  ShadowMountPlus reports the tile installed and registered, and the Home screen
  panel supports Add, Check and Remove. Removal deletes the tile
  folder, leaves `.ps5h-home-tile-removed`, uninstalls and rescans; a re-add
  restores it. The release keeps the plain HTTP 8791 and HTTPS 8790 listeners,
  a second same-artifact HTTP 8789 companion, the bearer gate and all 20 tools.
  The exact current `/data/harness.elf` downloaded from the console is
  **9,898,288 bytes**, sha256
  `f47fe099b7cb919561d2990dc67ff9c61f350a69a2397c4273c2ce8c6731e9e6`.
  At the live capture on 2026-11-18 17:00–17:14 PST, after a full power cycle,
  the primary process was PID 103 on 8791/8790; the 8789 companion was PID 107.
  `SIGSTOP` on PID 103 made 8791 and 8790 return `000` while 8789 stayed **200**;
  after `SIGCONT`, both main ports returned **200** and 8789 remained **200**.
  A separate stop test on PID 107 made 8789 return `000` while 8791 stayed
  **200**, and 8789 returned **200** after resume. A 17:06 PST `ps5h-live`
  recheck listed the same PIDs 103/107 and all three ports returned **200**; no
  second stop test was run because these were the already-tested processes. A
  final non-destructive check at 17:14 PST again returned **200** on all three.
  The
  earlier 2026-11-18 16:18 PST capture used PID 467 and a legacy input instance
  PID 302 on 8789;
  that is historical, not the current process. The console has no `/proc`, so the separate
  process-image hash is not observable; provenance is the launch path,
  PID/port ownership and executable file hash. The 16:26 outage was cleared by
  the 16:59 power cycle, and the current process was relaunched at 17:00.
  At 17:14 PST the current on-console tile folder has `eboot.bin` (5,211,435 bytes), `sce_sys/param.json`
  (333 bytes) and a 1,049,236-byte 512x512 RGBA `icon0.png`, all mode `0777`.
  The actual home-screen row was not observed from this remote session; the tile
  is verified through ShadowMountPlus and the app state. Full details are in
  [`docs/CONSOLE-REPORT.md`](docs/CONSOLE-REPORT.md).
- **Browser workspace, input and HTTPS** — historical `0.1.0-https` (source
  commit `d45b323`; exact capture time not recorded) served the 53,581-byte UI
  over **HTTPS on 8790** (`200`), while the then-retained **HTTP 8789**
  input-build listener and the then-new **HTTP 8791** listener both answered at
  that historical release. The HTTPS certificate chain validates with
  `curl --cacert <client-copy-of-ps5h-cert.pem>`; unauthenticated `/api/state` is `401` on
  both schemes, and the authenticated state reports build `0.1.0-https` with all
  20 tools and the unchanged 12-message session (7,611 context chars, three
  compactions). A real browser on the console HTTPS origin reported
  `window.isSecureContext: true`; clicking the mic button made exactly one
  `getUserMedia` call and left Chrome's microphone permission at `prompt` under
  “Preparing microphone…”, with no guidance fallback. With the permission granted
  and a mocked final speech result, `dictated text` landed at the caret in the
  composer and no page errors occurred. On the HTTP origin the same click
  reported `HTTPS or localhost required`, opened the guidance, and made zero
  microphone calls. At that historical `0.1.0-https` release (2026-11-18;
  exact clock time not recorded) `/data/harness.elf` was **9,761,576 bytes**, sha256
  `965d89549d80fb28e70f29cd54a8f05f5942e8d7c2d8b18485084696f3fb1d6b`
  (not the current artifact; see the current tile item above).
  The certificate and trust steps are in
  [HTTPS and microphone trust](#https-and-microphone-trust); measured counters
  are in [`docs/CONSOLE-REPORT.md`](docs/CONSOLE-REPORT.md).
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
- **Recon, plan, swarm and mission** — on `0.1.0-e0b6af4`, `--tools` listed 14
  tools with the plugin and `--plan --tools` listed 9 read-only tools (the
  current tree has six more built-ins and 12 read-only tools); a plan
  call to `fs_write` returned `unknown tool: fs_write` and created nothing.
  `env_info` reported `platform=ps5`, 16 CPUs, 2291924992 visible bytes, 17
  system mounts after filtering 1055 `/mnt/sandbox/...` entries, and the
  configured roots/skills directory. `fs_tree /data` marked the harness config
  and logs `denied`; `file_info /data/harness.elf` returned ELF64, x86-64,
  `ET_DYN` and the uploaded hash; `fs_strings` returned bounded strings. All six
  built-ins and both disk packs listed, and
  `reverse-engineer-anything` loaded its text. A nine-lane `swarm --call` was
  refused before spawning; a one-step mission with a missing profile parsed,
  ran, and returned `ok:false` at step `one`. On the `0.1.0-e6b6205` rebuild, a
  hostile PE header (`e_lfanew = 0xffffffff`) returned `signature:false`,
  `fs_tree` clamped `depth:1e999` to 8, and a mission with `max_steps:1e999`
  was rejected before execution. No provider key was staged, so no model-driven
  plan, swarm or mission turn ran on hardware.
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
- **The recovery path is not console-tested in this change.** Discovery,
  fixed-token/session relaunch, a changed `--serve` port and the home-tile
  rewrite still need a hardware run using [`docs/RECOVERY.md`](docs/RECOVERY.md).
  The page's lost-connection banner, same-origin automatic recovery, changed
  origin with token carry and transcript reload, and stale-token login are
  covered by `make reconnect-test` against local harnesses, not the console.
- **The packaged PHAR00001 title is uninstalled.** The packaged `eboot.bin` was
  not installed or launched from the shell; the separate HARN00001 home-screen
  tile is installed and registered (current evidence above), but it was not
  tapped from the home row. The packaged fake SELF, SFO key set and
  `--paid`/`--ptype` values still need a loader. A title launch also supplies no
  argv or environment, so the `file:` URI remains the runnable route.
- **Firmware and exploit versions are unrecorded.** No shell was available to
  query them.
- **USB and app-sandbox writes are untested.** No USB device was present and
  there was no title sandbox.
- **Peak memory during a 1 GB transfer is unmeasured.** The path streams in
  fixed chunks and the agent hashed the full 1 GiB file in 8 KiB chunks, but no
  RSS figure was captured on the console.
- **No model turn has run on hardware.** No provider key was available to the
  loader; the capability checks use `--call` over the same code paths. That
  also means no model-driven plan, swarm or mission turn has run on the
  console; the host e2e suite covers those loops against a stub.
- **CI has not executed.** The workflow is committed; no remote run yet.
- **No hosted provider has been used.** The round trip is verified against a
  local stub with the real client, policy, dispatch, deadline and redaction; a
  real account, real model and real tool-choice behaviour are untested.
- **Browser input has no hardware controller or real dictation test.** A
  Chromium browser loaded the historical `0.1.0-https` console HTTPS page, and the same test
  exercised the legacy HTTP fallback, the physical keyboard, mouse/OSK,
  synthetic DualSense, touchpad pointer and mocked speech paths. The HTTPS test
  profile bypassed the self-signed warning; the certificate was not installed in
  a normal browser profile, so the documented trust step is not itself tested.
  No controller was physically paired to the test browser, no real microphone
  recognition ran, and console-paired peripherals are not forwarded. Session-file
  persistence on the console is verified (12 messages after restart); no provider
  key was staged for a browser chat turn. There is still no native on-console
  window; `icon0.png` and the title metadata are the shell-facing surface.
- **No elevation helper**, so writes reach only what the process can already
  open.

## Roadmap

1. First console boot, TLS and `dlopen` proofs (P2–P3).
2. A launch path that supplies config, prompt and the model key to the title.
3. Manifest registry with an index and trust tiers (P5).
4. Console-side browser workspace validation: launch `--serve`, bind the LAN
   address from the payload, persist a session under `/data`, and drive one
   model turn from a phone. (P6)
5. Elevation helper for `/data/homebrew` writes (P4).

## License

MIT License. See [`LICENSE`](LICENSE).
