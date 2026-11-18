# Console run — 2026-11-18

First execution of the agent on real hardware. This is the evidence behind the
status claims in `README.md` and `platform/ps5/BUILD.md`; anything not listed
here is still unverified.

## Environment

| Item | Value |
|---|---|
| Console | PS5 at `192.168.4.100` (LAN) |
| Firmware | **not recorded** — no shell was available to query it |
| Exploit / loader | `elfldr` listening on 9021; web file manager on 8888. Exact versions not reported by either UI. |
| Date | 2026-11-18, ≈09:18–12:05 local |
| Host | macOS 26.6.2 (arm64), LLVM 23.1.4 |
| SDK | ps5-payload-sdk v0.45 (zip sha256 `5fac6409…`) |
| Libraries | PacBrew v0.41.1 (bundle sha256 `e811bc2e…`), libcurl 8.18.0, OpenSSL 3.5.2 |
| Agent build | `0.1.0-fd5d0ae` (P2/P3 and 1 GiB); `0.1.0-4d7499f` (local agents); `0.1.0-c0f76b5` (delegation); `0.1.0-653d046` (skills, `fs_edit`); `0.1.0-e0b6af4` (recon, plan, swarm, missions); `0.1.0-e6b6205` (memory-safety rebuild); `0.1.0-01c6cbc` (home-screen tile); `0.1.0-final` (LAN workspace baseline); `0.1.0-input` (browser input layer); **`0.1.0-https`** (current HTTPS browser workspace) |

Current artifacts on disk (`0.1.0-https`):

| File | Bytes | sha256 |
|---|---|---|
| `harness.elf` (HTTPS process) | 9,761,576 | `965d89549d80fb28e70f29cd54a8f05f5942e8d7c2d8b18485084696f3fb1d6b` |
| `eboot.bin` (not installed) | 7,773,504 | `3109fa9bed23e80bc08652f588f4b65eefc5078d78e34c576faecd39f95a9a82` |
| `sce_sys/param.sfo` | 376 | `6bf5d47549c9314c6cf5b5355cae17f1603c210ff6aaafa52978221c6e19803f` |
| `sce_sys/icon0.png` | 30,867 | `72b716d302f400a14cc9a879f7ec3474281a5f7127663fd2bc395d53cdd1a323` |

The P2/P3 and 1 GiB evidence below was collected on `0.1.0-fd5d0ae`; the
local-agent checks use `0.1.0-4d7499f`; the delegation checks use
`0.1.0-c0f76b5`; the skills and edit checks use `0.1.0-653d046`; the recon,
plan, swarm and mission checks use `0.1.0-e0b6af4`; the parser-hardening smoke
uses `0.1.0-e6b6205`; the home-screen tile is from `0.1.0-01c6cbc`;
the browser input layer is from `0.1.0-input` (source commit `8baf87a`);
the HTTPS workspace is from `0.1.0-https` (source commit `d45b323`).

## How it was launched

The package's `harness.elf` was uploaded to `/data/harness.elf` through the web
file manager and started by `elfldr` with a `file:` URI. Arguments follow
`?args=`; spaces are percent-encoded and inner spaces backslash-escaped because
elfldr splits on unescaped spaces:

```bash
printf 'file:/data/harness.elf?args=--config%%20/data/ps5-harness.json%%20--call%%20http_get%%20--args%%20%%7B%%22url%%22%%3A%%22https%%3A%%2F%%2Fexample.com%%2F%%22%%7D\n' \
  | nc -w 60 192.168.4.100 9021
```

`/data/ps5-harness.json` allowed `/data` and `/user`, with `plugins_dir` set to
`/data`. No API key was available to the loader, so no model turn was run; the
capability checks use the shipped `--call` path, the same HTTP, filesystem,
download and plugin code the agent loop invokes.

## Two defects the console exposed

1. **`harness.elf` was ET_EXEC.** `make_fself.py` needs a fixed-address ELF, but
   elfldr only maps ET_DYN; the first launch failed with
   `[elfldr.elf] Error spawning payload`. Commit `2e6d21c` keeps
   `harness.elf` as the PIE link and packs `eboot.bin` from a second non-PIE
   link; `ps5_package_test` now asserts ET_DYN.
2. **`sha256_file_hex` used a 64 KiB stack buffer.** A download with an expected
   digest transferred every byte and then died before printing or renaming,
   even for a 577-byte body. Commit `fd5d0ae` uses an 8 KiB buffer. After the
   fix the same 10 MiB resume returned `status: 206`, the correct digest, and
   renamed the file; the 1 GiB transfer below then finalized on the same build.

## P2 — boot and resources

`harness.elf` starts, appends to `/data/ps5-harness-agent.log`, and exits
cleanly (`--call` returns 0; no `CRASH` line). Observed lines:

```
build=0.1.0-fd5d0ae title=PHAR00001
cpu online=16 configured=16
mem total=2185 MiB available=unknown page=16384 B
limit nofile=13952/13952 as=unlimited/unlimited data=34359738368/34359738368
mount /data type=nullfs free=342236 MiB total=642647 MiB
candidate /data free=342236 MiB total=642647 MiB
candidate /user/homebrew unavailable errno=2
candidate /mnt/usb0 free=1 MiB total=2 MiB
candidate /mnt/usb1 free=1 MiB total=2 MiB
budget workers=16 cpu=16 buffered=136 MiB file-read=4 MiB file-size=unlimited
```

The bring-up probe, built from the same source at the same commit, also
reported `pid=134 uid=1`, `cwd=/`, `argc=1`, 16 threads created of 16 requested,
and `alloc request=33554432 ok=1` after its last-byte check was fixed. The
budget's 136 MiB buffered share is therefore below the 256 MiB that a cumulative
64 KiB allocation test could reserve.

Not met: the title-install path. `eboot.bin` was not installed or launched from
the shell, so the SFO/icon rows and the fake-SELF acceptance remain untested.

## P3 — capabilities

### TLS — verified

- `http_get https://example.com/` returned **200** with the real page body.
- `http_get https://expired.badssl.com/` failed with
  `request failed: SSL peer certificate or SSH remote key was not OK`, which is
  curl's `CURLE_PEER_FAILED_VERIFICATION`. Verification is enabled; the CA
  bundle was found at one of the paths `console_compat.cpp` probes
  (`PS5H_CA_BUNDLE`, the sandbox `common/cert/CA_LIST.cer`, or
  `/system/common/cert/CA_LIST.cer`).
- DNS resolved from the console and the clock accepted the current certificate,
  so both were valid at test time. The exact CA path used is not logged.

### `dlopen` — verified

`examples/plugins/hello/hello.c` was built with the payload SDK as
`hello.so`, uploaded to `/data`, and loaded by the agent (`plugins_dir` =
`/data`). `--tools` listed `hello_echo`; `--call hello_echo '{"value":"console"}'`
returned `{"echo":{"value":"console"}}`.

### Writes — verified in three locations, USB absent

| Destination | Result |
|---|---|
| `/data/ps5-harness-write.txt` | wrote 17 bytes, read back correctly |
| `/data/homebrew/ps5-harness-write.txt` | wrote 14 bytes, read back correctly |
| `/user/ps5-harness-write.txt` | wrote 10 bytes, read back correctly |
| `/mnt/usb0` | no USB inserted; the 2 MiB tmpfs mount existed but opening a file there failed |
| app's own folder | not tested (no title sandbox) |

## P4 — transfers

- **Small file** — `https://example.com/` → 577 bytes, `ok: true`, sha256
  `25ddf2c883e0d1958ea971d279a7e4f0fd446724ee3db7db19dadabd4a62e484` matched,
  renamed into place.
- **Resume** — a 1 MiB prefix of `/data/dl-10mb-ovh.bin.part` was seeded, then
  `download` fetched `https://proof.ovh.net/files/10Mb.dat` with the expected
  digest. Result: `status: 206`, `bytes: 10485760`, matching sha256
  `e5b844cc57f57094ea4585e235f36c78c1cd222262bb89d53c94dcb4d6b3e55d`, renamed.
  The Range request was served and the transfer did not restart at zero.
- **Free-space cap** — `/mnt` is a 2 MiB tmpfs. A 10 MB download to
  `/mnt/dl.bin` stopped after 1,711,443 bytes with
  `download exceeds the configured byte limit`. The `.part` is retained on
  purpose so a later call can resume.
- **1 GB** — see below.
- **Peak memory** — not measured. The download path streams through curl's
  write callback to a `FILE*` and hashes in 8 KiB chunks, so no buffer grows
  with file size, but no RSS figure was captured on the console.

### 1 GB transfer — post-fix

- **Build** — `0.1.0-fd5d0ae`, the fixed digest path.
- **Source** — `https://proof.ovh.net/files/1Gb.dat`, 1,073,741,824 zero bytes,
  Range-capable. It is the same all-zero source used for the earlier
  interrupted attempt.
- **Setup** — 1,000 MiB (1,048,576,000 bytes) of the same zero bytes was staged
  as `/data/dl-1gb-final.bin.part`, so the agent had a real partial to resume
  rather than an already-complete file.
- **Agent result** — the shipped `download` tool printed this over elfldr:

  ```json
  {
    "bytes": 1073741824,
    "ok": true,
    "sha256": "49bc20df15e412a64472421e13fe86ff1c5165e18b2afccf160d4dc19fe68a14",
    "status": 206
  }
  ```

- **Checks** — `status` is `206` (Range resume); `bytes` is exactly
  **1,073,741,824**; the agent hashed the whole file itself and the digest
  matches 1 GiB of zero bytes; it renamed the `.part` to
  `/data/dl-1gb-final.bin`; the file manager lists that file at 1,073,741,824
  bytes and no `.part` remains; the payload completed in 42 s and exited
  cleanly with no crash line. The on-console startup log records
  `build=0.1.0-fd5d0ae`; the result above is the agent's stdout from the same
  process.
- **Peak memory** — not measured. The transfer streams through curl's write
  callback to a `FILE*` and the digest uses an 8 KiB buffer; no buffer grows
  with file size.

## Local agents — build 0.1.0-4d7499f

The new `/data/harness.elf` was uploaded and two profiles were staged in
`/data/homebrew`, the console default, so no `--agents` flag was needed:

- `archivist.json` — inline `system_prompt`.
- `maintainer.json` + `maintainer.md` — `system_prompt_file`.

`--list-agents` printed both:

```
agents_dir=/data/homebrew
archivist                Local archivist: inspect files and summarise what is on the console.
maintainer               Local maintainer: keep a short maintenance log for the console.
```

`--agent-info archivist` resolved the inline prompt (249 bytes), model
`gpt-4o-mini`, temperature 0.2, `max_tokens` 1024 and `max_steps` 8. Then
`archivist.json` was overwritten on the console with a v2 profile; the next
`--list-agents` on the same binary, with no rebuild, printed
`Local archivist v2: edited on the console.` and `--agent-info` showed the v2
prompt (212 bytes). `maintainer --agent-info` loaded `maintainer.md` from the
profiles directory (345 bytes), proving the prompt-file path.

A `--call fs_list` run confirmed the on-console log records
`build=0.1.0-4d7499f`. No provider key was staged, so no model turn was run;
the host e2e test asserts that a loaded profile's prompt, model, temperature
and token cap reach the provider request.

## Delegation — build 0.1.0-c0f76b5

The delegation build was cross-compiled from commit `c0f76b5` and uploaded to
`/data/harness.elf`. The on-console log records:

```
[harness] build=0.1.0-c0f76b5 title=PHAR00001
```

`--tools` listed seven tools, including the model-callable delegation entry:

```
7 tools registered
  delegate  (3600000 ms)
```

`--list-agents` still read the console profile directory:

```
agents_dir=/data/homebrew
archivist                Local archivist v2: edited on the console.
maintainer               Local maintainer: keep a short maintenance log for the console.
```

`--agent-info archivist` still resolved the profile, now with the delegation
depth from the main config:

```
name=archivist
description=Local archivist v2: edited on the console.
model=gpt-4o-mini
temperature=0.200
max_tokens=1024
max_steps=8
delegation_max_depth=2
system_prompt_source=profile
system_prompt_bytes=212
```

A direct `--call delegate` with a missing profile exercised the fail-closed
path on hardware. It returned `ok: false` and wrote both the attempt and the
refusal to the log; the main config's step/token pool is visible in the result:

```
[harness] delegate call profile=missing from_depth=0 max=2
[harness] delegate ! profile=missing depth=1 error=cannot load agent: cannot read /data/homebrew/missing.json
{
  "depth": 1,
  "error": "cannot load agent: cannot read /data/homebrew/missing.json",
  "ok": false,
  "profile": "missing",
  "remaining_steps": 4,
  "remaining_tokens": 512
}
```

No provider key was staged, so a real parent→child model conversation was not
run on the console. The host e2e suite drives that path through the stub
provider, including the parent `delegate` tool call, the child profile request,
a child `fs_read` refused by the inherited parent root, the child answer
returned to the parent, max-depth refusal, and exact step/token refunds.

## Skills and targeted edits — build 0.1.0-653d046

This build was cross-compiled from commit `653d046`, uploaded to
`/data/harness.elf`, and its two example packs were staged under
`/data/homebrew/skills/` through the file manager's nested upload path. The log
records the new build:

```
[harness] build=0.1.0-653d046 title=PHAR00001
```

`--tools` listed nine tools: the six built-ins (`fs_list`, `fs_read`,
`fs_write`, `fs_edit`, `http_get`, `download`), the plugin's `hello_echo`, the
new `skill` tool and `delegate`. The two relevant descriptions are:

```
fs_edit  (30000 ms)
    Replace exact text in a file inside the write roots. Refuses a missing or
    ambiguous match; pass replace_all to change every occurrence. ...
skill  (30000 ms)
    List local skills, or load one skill's instructions by name. ...
```

`--call skill` with no name listed both packs, including their descriptions:

```json
{
  "ok": true,
  "skills": [
    {"description": "Create, append to and review a plain-text notes file ...", "name": "console-notes"},
    {"description": "Audit one directory on the console and report ...", "name": "file-audit"}
  ]
}
```

Loading `console-notes` returned the 1,127-byte `SKILL.md` body (frontmatter
removed, `clipped: false`). Loading
`{"name":"file-audit","resource":"report-template.md"}` returned the bundled
235-byte template. A traversal resource,
`{"name":"console-notes","resource":"../../etc/hosts"}`, failed closed:

```json
{"error": "resource is outside the skill directory", "ok": false}
```

`--agent-info archivist` now also prints the selected skills root:

```
name=archivist
model=gpt-4o-mini
max_steps=8
delegation_max_depth=2
skills_dir=/data/homebrew/skills
```

On the same build, `fs_write` created `/data/ps5-harness-edit.txt` with 16 bytes
(`alpha old omega`). `fs_edit` with `old=old`, `new=new` returned
`{"bytes": 16, "path": "/data/ps5-harness-edit.txt", "replaced": 1}`, and a
follow-up `fs_read` returned `alpha new omega`. A file containing `x x` then
refused a two-match edit:

```
refused: old text appears 2 times in /data/ps5-harness-edit.txt; include more context or pass replace_all
```

An edit to `/etc/hosts`, outside the write roots, returned
`refused: path is outside the write roots`. Both refusals happened before any
write.

No provider key was staged, so the model did not choose a skill or an edit on
hardware. The host e2e suite drives both tools through the real CLI and asserts
that a delegated child's tool schema still contains `skill` and `fs_edit`.

## Home-screen tile — `0.1.0-01c6cbc`

The generated `icon0.png` was replaced with a 512x512 PS5-style tile: near-black
cool gradient, soft vignette, a white H monogram with a glass highlight and one
blue bridge. It is rendered at 2x and box filtered, so it stays legible at the
128px size used in a home row; the title is shell-rendered rather than baked in.
This is a package-art change only, validated by `ps5_package_test`. The actual
home-screen rendering has not been observed because the title-install path
remains unverified.

## Memory-safety rebuild — `0.1.0-e6b6205`

The parser hardening was rebuilt into a fresh payload and re-smoked on the
console without a provider key. `--tools` listed the same 14 tools. A 64-byte
PE file with `e_lfanew = 0xffffffff` returned `format: pe` and
`signature: false` instead of throwing; `fs_tree` with `depth: 1e999` returned
`depth: 8` and `truncated: true`; and a mission with `max_steps: 1e999` was
rejected with `max_steps must be between 0 and 64` before any child started.
The full fuzz and sanitizer results are in the host logs, not on the console.

## Local recon, plan, swarm and mission

On `0.1.0-e0b6af4`, `--tools` listed 14 tools (the seven original built-ins,
four recon tools, `delegate`, `swarm`, and the loaded `hello_echo` plugin).
`--plan --tools` listed 9: `fs_list`, `fs_read`, `skill`, `env_info`, `fs_tree`,
`file_info`, `fs_strings`, `delegate`, `swarm`. No write, edit, HTTP, download,
endpoint or plugin tool was present. A plan-mode `--call fs_write` returned
`unknown tool: fs_write`; the requested `/data/plan-should-not-exist.txt` did
not exist afterwards.

`env_info` returned `platform: ps5`, `build: 0.1.0-e0b6af4`,
`skills_dir: /data/homebrew/skills`, 16 online CPUs, 2,291,924,992 visible
bytes, and a bounded mount list:

```json
"mounts_total": 1072,
"mounts_shown": 17,
"sandbox_mounts_omitted": 1055,
"mounts_omitted": 0
```

`fs_tree /data` (depth 1) marked `ps5-harness.json`, the harness logs and
`ps5-harness-edit.txt` as `denied`, without reading them. `file_info` on the
uploaded `/data/harness.elf` returned:

```json
{"format":"elf", "ok":true, "size":9499552,
 "sha256":"8377fb70f2dd45e542eba1eb72c11282d542c04be3f9d2c3f323480b9ac9fbf5",
 "elf":{"class":"ELF64","machine":"x86-64","type":"ET_DYN","entry":"0x0"}}
```

The digest matches the host artifact. `fs_strings` read all 9,499,552 bytes,
returned its capped string list and did not execute the binary. `skill` listed
eight packs: the six built-ins plus the staged `console-notes` and
`file-audit`; loading `reverse-engineer-anything` returned its instructions and
an empty resource list.

A nine-lane `--call swarm` was refused before any child started:

```json
{"error": "swarm width exceeds the hard limit of 8", "ok": false}
```

A one-step mission with profile `does-not-exist` parsed and ran, then stopped:

```
[harness] mission start name=probe steps=1
[harness] mission step=one profile=does-not-exist
[harness] delegate ! profile=does-not-exist depth=1 error=cannot load agent: cannot read /data/homebrew/does-not-exist.json
[harness] mission <- ok=false failed=one
```

No provider key was staged, so no model-driven plan, swarm or mission turn was
run on the console. The host e2e suite covers those loops with the real CLI,
provider history, tool dispatch and budget accounting.

## P5 — extension contract

The `hello` plugin was built outside the package with only
`include/ps5h/plugin_api.h` and the SDK, then loaded by the console binary. The
ABI check (`host->abi_version >= 2`), tool registration and handler execution
all worked. Plugins are in-process; a plugin that crashes takes the harness with
it. That isolation choice was not tested with a crashing plugin.

## Rest mode — code evidence

The harness is an elfldr payload, not a service that can outlive the console's
userland. Since this report was captured, `--serve` added an explicit
long-lived LAN mode; it still only runs when a human launches `harness.elf`, and
it is not a rest-mode service. The evidence is in the tree, not only in this
report:

| Claim | Reference |
|---|---|
| The artifact is built for elfldr with a `file:` URI | `platform/ps5/Makefile:89`; `platform/ps5/BUILD.md:197` |
| The console run used that path | `docs/CONSOLE-REPORT.md:36` (uploaded through the web file manager, started by `elfldr`) |
| The harness process ends after one command or one agent run | `src/main.cpp:561` (entry); returns at `:1013` (`--call`), `:1034` (`--tools`),   `:1039` (no prompt) and `:1050` (agent run), plus the mission return at `:1010` |
| The harness opens no listener by default | `src/server.cpp` binds only in the explicit `--serve` path; `src/http.cpp` remains the outbound libcurl client |
| `--serve` requires a bearer token and is opt-in | `src/server.cpp` rejects every unauthenticated `/api/*` request; `docs/SECURITY.md` invariant 15 |
| It is not a rest-mode service | the listener starts only after a human launches the payload with `--serve`; nothing registers it with the OS |
| 9021 and 8888 belong to other processes | `docs/CONSOLE-REPORT.md:13` (elfldr and the web file manager) |

Suspending the console stops that userland, so the harness process, elfldr and
the file manager are not running to receive anything. Nothing in the repository
registers a background service, wake source or scheduler, so no tool call,
skill, edit or model turn can execute while the console sleeps.

The only workable path for work during sleep is an always-on host queue: keep
the jobs on a machine that stays awake, wake the console, launch `harness.elf`,
then run the queue through it. This report did not suspend the console — waking
it requires a human at the machine — so the limitation is established from the
launch path and process lifetime above, not from a sleep test.

## 0.1.0-final LAN workspace (baseline)

This was the final end-to-end run before the browser input layer. It was the
only listener left on the console at that release. The model was **stubbed**;
the harness and every tool call were the real console build.

### Artifact and build

```bash
PATH=/opt/homebrew/opt/llvm/bin:$PATH \
  make -B -C platform/ps5 BUILD_ID=0.1.0-final
```

Source commit for the code in this run: `a4a947a` (the responsive-panel fix).
The documentation commit follows and does not change payload bytes. The final
artifact staged to `/data/harness.elf` for that release was:

| File | Bytes | sha256 |
|---|---|---|
| `harness.elf` (was running) | 9,727,192 | `5e60306dcdaf6ac4cb336e1877880c57fba45a23428b20ef2c71270145a12ef8` |
| `eboot.bin` (not installed) | 7,733,984 | `f65de21248d82a60bfc3ba5d0f606c0de522101683697e461757656b36b2c2f4` |

`make -B` was rerun to check byte reproducibility; the PS5 linker embeds a
per-link build-id, so the hash changes between identical links. The table above
is the exact binary that was staged and is running, not a claim of reproducible
bytes. `ps5_package_test` passed on the package.

### Endpoint and access control

| Check | Measured |
|---|---|
| `GET /` | **200**, 26,044 bytes |
| `GET /api/state` without a token | **401** `{"error":"unauthorized","ok":false}` |
| `GET /api/state` with the bearer token | **200**, build `0.1.0-final` |
| Registered tools | **20** |

The 20 tools were `fs_list`, `fs_read`, `fs_write`, `fs_edit`, `http_get`,
`download`, `skill`, `env_info`, `fs_tree`, `file_info`, `fs_strings`,
`fs_mkdir`, `fs_move`, `fs_delete`, `fs_search`, `hexdump`, `sysctl`,
`hello_echo`, `delegate` and `swarm`. The plugin was loaded from `/data`.

### Real console tools through `/api/chat`

The console was started with `--serve 8789 --context-chars 4096` and a fresh
session. A local OpenAI-compatible stub was exposed to the console over an HTTPS
`trycloudflare.com` tunnel. The stub chose the tool calls and generated the
summaries; it did **not** execute any tool. The harness performed every call on
the console. The temporary stub and tunnel were stopped after the run; the listener was later replaced by the input build below.

Turn 1 returned **200**, used **4 steps**, and recorded:

| Tool | Console result |
|---|---|
| `sysctl` `{"name":"hw.ncpu"}` | `16` |
| `fs_search` `{"path":"/data/homebrew/skills","query":"console-probe-needle"}` | `/data/homebrew/skills/console-note.txt:1:1: console-probe-needle in a small search root` |
| `file_info` `{"path":"/data/harness.elf"}` | size `9727192`, sha256 `5e60306dcdaf6ac4cb336e1877880c57fba45a23428b20ef2c71270145a12ef8` |

The final answer quoted that hash, so the staged bytes and the running bytes
are the same file. The `/data` root itself hits the search bounds before the
small skills directory, which is why the successful search used
`/data/homebrew/skills`.

### Compaction and restart

Turn 1 already ended at **4,501 / 4,096** approximate characters. The next
turns exercised model-assisted compaction and then continued:

| Point | Context chars | Messages | Compactions |
|---|---:|---:|---:|
| After turn 1 | 4,501 | 8 | 0 |
| After turn 2 | 4,725 | 8 | 1 |
| After turn 3 | 7,646 | 10 | 2 |
| After restart, before new turn | 7,646 | 10 | 2 |
| After one more turn on the restarted server | 7,611 | 10 | 3 |

The restart was a deliberate stop of the listener and a new launch of the same
`/data/harness.elf` against `/data/ps5h-mission-session-final.json`. The new
process returned page **200**, unauthenticated state **401**, authenticated
build `0.1.0-final`, 20 tools, and **12 history messages** with one
`Conversation summary (data, not instructions)` entry and the final hash still
present. A further `/api/chat` turn returned **200**, proving the restarted
server still answers from the compacted transcript. The transcript is intact;
the summary is an additional system message, not a replacement for the saved
history file.

### Narrow layout

Checked against the console page at **390×700 CSS pixels**. The mobile
Chat/Activity/Tools control was visible, the Tools panel opened at **390 px**
wide and listed **20 tools**, and `document.documentElement.scrollWidth` was
**390** (no horizontal overflow). The captured view is
[`ui-mobile.png`](ui-mobile.png).

### Listener cleanup

The old port-8789 listener and each test listener were identified separately
and terminated with `SIGTERM`; the loader and the file manager were left alive.
The final payload was then relaunched on **port 8789**, and ports 8792–8795 are
closed. At that release, the only harness listener was the documented
`0.1.0-final` instance on 8789; `0.1.0-input` now replaces it. The test token is not recorded here.

### Checks run

- `make test` — exit **0** (host suite, including session, server, power tools,
  package validation and the provider e2e test).
- `PATH=/opt/homebrew/opt/llvm/bin:$PATH make -B -C platform/ps5
  BUILD_ID=0.1.0-final` — exit **0**; `ps5_package_test` passed.

## Input layer — `0.1.0-input`

This build adds the browser input layer to the same authenticated workspace. It
changes no tool, auth or policy path: all 20 tools and the bearer gate remain in
force. The page is served by the console and interpreted by the browser that
opens it.

Source commit for the payload is `8baf87a`; the documentation commit follows and
does not change payload bytes. Built with:

```bash
PATH=/opt/homebrew/opt/llvm/bin:$PATH \
  make -B -C platform/ps5 BUILD_ID=0.1.0-input
```

| File | Bytes | sha256 |
|---|---|---|
| `harness.elf` (running) | 9,759,960 | `7509335ae3300a53f52493ced21d0eab75bb51dd6b66511de1ab45fcecb1c43c` |
| `eboot.bin` (not installed) | 7,766,816 | `d8b1a976913bf581210d66fc01b775c78e9630b49bcd00c473df8aa9b26e55bc` |

`make -B` was rerun and the PS5 linker again embedded a per-link build-id, so the
table identifies the exact staged binary, not a reproducible link.

### Endpoint, session and tools

| Check | Measured |
|---|---|
| `GET /` | **200**, 53,581 bytes |
| `GET /api/state` without a token | **401** `{"error":"unauthorized","ok":false}` |
| `GET /api/state` with the bearer token | **200**, build `0.1.0-input` |
| Registered tools | **20** |
| Saved history | **12 messages**; context **7,611 / 4,096** chars, **3 compactions** |

The saved `0.1.0-final` session was carried into the input build rather than
reset, so the transcript and its summary remain available. The build opens the
same Chat/Activity/Tools workspace and the same 20 tool schemas. A real console
model turn was not rerun in this build; the earlier `sysctl`, `fs_search` and
`file_info` evidence above is from `0.1.0-final`.

### Browser input checks

A headless Chromium test against the host build exercised the client-side paths
without changing the console page:

| Path | Result |
|---|---|
| Physical keyboard | Typed `keyboard works` into the composer |
| Mouse + on-screen keyboard | Opened the OSK and clicked `q`; composer became `keyboard worksq` |
| DualSense | Pill read `INPUT · DUALSENSE`; X opened the OSK, focus landed on `q`, A inserted `q` |
| Touchpad | Synthetic touch `(0.25, 0.25)` moved the visible pointer to `800,500` in a 1280×800 viewport |
| Dictation | Mocked Web Speech inserted `dictated text`; no page errors |

The mocked dictation avoids a real permission prompt. On the default LAN HTTP
origin `window.isSecureContext` is false, so the page shows help and does not
request the microphone. Real microphone recognition, a physically connected
controller and console-paired peripherals were not tested; the browser reads
input from the device running the page, not from peripherals paired only to the
PS5. Web Speech works on HTTPS or localhost.

### Listener

The old 8789 listener was stopped by signalling its individual PID, not by a
name/group kill. The exact uploaded `0.1.0-input` ELF was then launched on 8789
with the existing mission config, session and context limit. It is the only
harness listener; test listeners 8792–8795 remain stopped.

### Checks run

- `make test` — exit **0** (host suite, including the input assertions,
  package validation and provider e2e test).
- `PATH=/opt/homebrew/opt/llvm/bin:$PATH make -B -C platform/ps5
  BUILD_ID=0.1.0-input` — exit **0**; `ps5_package_test` passed.
- Console artifact read back through `file_info` — hash and size match the table.
- Headless browser input test — all five paths above, no page errors.

## HTTPS workspace — `0.1.0-https`

This build adds an optional TLS listener to the same browser workspace. It does
not change the page, auth gate, tools or session format; it makes the existing
page a secure context so Web Speech and the microphone permission prompt work.

Built with:

```bash
PATH=/opt/homebrew/opt/llvm/bin:$PATH \
  make -B -C platform/ps5 BUILD_ID=0.1.0-https
```

Source commit is `d45b323`; the documentation commit follows and does not
change payload bytes.

### Artifact and certificate

| Item | Value |
|---|---|
| HTTPS URL | `https://192.168.4.100:8790/` |
| New HTTP URL | `http://192.168.4.100:8791/` |
| Retained HTTP URL | `http://192.168.4.100:8789/` (existing `0.1.0-input` process) |
| Certificate | `/data/ps5h-cert.pem` |
| Private key | `/data/ps5h-key.pem` |
| Certificate | self-signed local root, `CN=PS5 Harness`; SAN `192.168.4.100`, `127.0.0.1`, `localhost` |
| Certificate SHA-256 | `8C:80:7E:62:8F:B0:4B:CB:70:25:7C:6A:97:06:43:CD:96:63:24:FF:C7:CA:67:DE:79:32:49:9A:7E:BD:F7:B3` |
| `harness.elf` | 9,761,576 bytes, sha256 `965d89549d80fb28e70f29cd54a8f05f5942e8d7c2d8b18485084696f3fb1d6b` |
| `eboot.bin` (not installed) | 7,773,504 bytes, sha256 `3109fa9bed23e80bc08652f588f4b65eefc5078d78e34c576faecd39f95a9a82` |

`curl --cacert /data/ps5h-cert.pem https://192.168.4.100:8790/` returned
**200** and the page was 53,581 bytes. The self-signed certificate must be
trusted by the browser for a warning-free normal session; the exact macOS and
iOS steps are in the README’s **HTTPS and microphone trust** section. The
browser evidence below used a test-profile certificate bypass because that
temporary profile did not have the certificate installed; the TLS chain itself
was validated by `curl --cacert`.

### Access control and session

| Check | HTTPS 8790 | HTTP 8791 | Legacy HTTP 8789 |
|---|---:|---:|---:|
| `GET /` | **200** | **200** | **200** |
| `/api/state` without token | **401** | **401** | **401** |
| Authenticated build | `0.1.0-https` | `0.1.0-https` | `0.1.0-input` |
| Tools | **20** | **20** | **20** |
| History | **12 messages** | **12 messages** | **12 messages** |

The new process loaded the same `/data/ps5h-mission-session-final.json` and
reported context **7,611 / 4,096** characters, **3 compactions**, and **10
non-system messages** (12 total including the system prompt and summary). The
legacy 8789 process remains up because its PID could not be identified safely
without risking the loader or file manager; the new payload is reachable on both
8790 and 8791, so the existing HTTP path did not break.

### Browser checks on the deployed origin

A Chromium test loaded the real console HTTPS URL, not the host build:

| Path | Result |
|---|---|
| Secure context | `window.isSecureContext === true` |
| Mic button | `permissionBefore=“prompt”`; one `getUserMedia` call; permission stayed `“prompt”`; status `Preparing microphone…`; no guidance fallback |
| Mocked final result | `dictated text` landed at the composer caret; status `Listening…`; no page errors |
| HTTP fallback | `window.isSecureContext === false` on legacy 8789; status `HTTPS or localhost required`; guidance opened; **0** `getUserMedia` calls |

The mocked result used a fake `SpeechRecognition` object after the real
microphone request/allowed device path. It did not prove a cloud speech
recognition result; it proved the page’s result-to-caret insertion path.

### Checks run

- `make test` — exit **0** (host suite, including the corrected session test,
  server test, package validation and provider e2e test).
- `ps5_package_test .deps/ps5-build/PHAR00001` — exit **0**, package reports
  `eboot.bin` 7,773,504 bytes and `harness.elf` 9,761,576 bytes.
- TLS chain validation with `curl --cacert` — page **200**; certificate and
  subject/SAN/fingerprint checked.
- HTTPS/HTTP access-control table above — **401** unauthenticated, **20** tools,
  **12** history messages.
- Deployed-origin Chromium test — secure context, prompt, mocked dictation and
  HTTP fallback as above, with **no page errors**.

## Still unverified

- Title install and launch of `eboot.bin` (fake SELF, SFO keys, shell entry).
- Console firmware and exploit versions; the loader and web file manager
  versions.
- USB writes and an app-sandbox write.
- Peak RSS during a large transfer.
- A provider-backed model turn on hardware; the final console turn used the
  labeled local stub because no provider key was available to the loader.
- A delegated parent→child model turn, a model-driven plan, a swarm with
  provider-backed lanes, or a multi-step mission with model turns on hardware;
  the tools, parser, fail-closed runner and one-step failure are
  console-verified, but no provider call was made.
- The exact CA bundle path and resolver used.
- Rest mode. The harness is a userland payload; it has no path to keep running
  while the console is suspended, and this build was not tested by entering
  rest mode. Wake the console and relaunch the payload.
