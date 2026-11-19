# Console run — 2026-11-18

First execution of the agent on real hardware. This is the evidence behind the
status claims in `README.md` and `platform/ps5/BUILD.md`; anything not listed
here is still unverified.

> **Historical evidence notice.** Except for the current `0.1.0-tile` subsection
> below, this report records earlier builds and captures from 2026-11-18. The
> initial run window was approximately 09:18–12:05 local; the legacy 16:18
> capture and the current 17:00–17:14 capture are identified in their sections.
> Early launch PIDs were not recorded unless stated. The current live process is
> PID 103 on 8791/8790 and PID 107 on 8789; the PID list was rechecked at 17:06
> PST and the service/probe/tile at 17:14 PST. Any value
> without that current label is historical, not a claim about today's process.

## Environment

| Item | Value |
|---|---|
| Console | PS5 at `192.168.4.100` (LAN) |
| Firmware | **not recorded** — no shell was available to query it |
| Exploit / loader | `elfldr` listening on 9021; web file manager on 8888. Exact versions not reported by either UI. |
| Date | 2026-11-18, ≈09:18–12:05 local (historical initial run; later captures are timestamped separately) |
| Host | macOS 26.6.2 (arm64), LLVM 23.1.4 |
| SDK | ps5-payload-sdk v0.45 (zip sha256 `5fac6409…`) |
| Libraries | PacBrew v0.41.1 (bundle sha256 `e811bc2e…`), libcurl 8.18.0, OpenSSL 3.5.2 |
| Agent build | **Current: `0.1.0-tile`** (installed home-screen tile, browser workspace). Historical builds: `0.1.0-fd5d0ae` (P2/P3 and 1 GiB); `0.1.0-4d7499f` (local agents); `0.1.0-c0f76b5` (delegation); `0.1.0-653d046` (skills, `fs_edit`); `0.1.0-e0b6af4` (recon, plan, swarm, missions); `0.1.0-e6b6205` (memory-safety rebuild); `0.1.0-01c6cbc` (home-screen tile package art); `0.1.0-final` (LAN workspace baseline); `0.1.0-input` (browser input layer); `0.1.0-https` (HTTPS browser workspace); `0.1.0-chat` (PS5 chat presentation) |

Local build outputs (current payload; historical package staging):

| File | Bytes | sha256 |
|---|---|---|
| `harness.elf` (current live process, 17:14 PST recheck) | 9,898,288 | `f47fe099b7cb919561d2990dc67ff9c61f350a69a2397c4273c2ce8c6731e9e6` |
| `eboot.bin` (local PHAR00001 package staging; not the installed tile shell) | 7,906,384 | `730cf9aae8511c71b5631a804f56c1d996559b0a0819184f4edc70f671ece765` |
| `sce_sys/param.sfo` (local package staging) | 376 | `6bf5d47549c9314c6cf5b5355cae17f1603c210ff6aaafa52978221c6e19803f` |
| `sce_sys/icon0.png` (local package staging) | 30,867 | `72b716d302f400a14cc9a879f7ec3474281a5f7127663fd2bc395d53cdd1a323` |

The P2/P3 and 1 GiB evidence below was collected on `0.1.0-fd5d0ae`; the
local-agent checks use `0.1.0-4d7499f`; the delegation checks use
`0.1.0-c0f76b5`; the skills and edit checks use `0.1.0-653d046`; the recon,
plan, swarm and mission checks use `0.1.0-e0b6af4`; the parser-hardening smoke
uses `0.1.0-e6b6205`; the home-screen tile is from `0.1.0-01c6cbc`;
the browser input layer is from `0.1.0-input` (source commit `8baf87a`);
the HTTPS workspace is from `0.1.0-https` (source commit `d45b323`); the PS5
chat presentation is `0.1.0-chat`; the installed home-screen tile is
`0.1.0-tile`, built from the current worktree with an explicit build id. The
final artifact was rebuilt from a clean tree (`make clean`,
`make -C platform/ps5 clean`, then the cross-build with
`BUILD_ID=0.1.0-tile`); the deployed ELF is byte-identical to that build. The
local clean artifact is dated 2026-11-18 15:51:38; no file under `src/`,
`include/`, `tests/`, `platform/` or `Makefile` is newer, and this capture did
not rebuild or redeploy it.

## How it was launched — historical initial 0.1.0-fd5d0ae run

> Historical: this command and config describe the initial run, not the current
> `0.1.0-tile` process. The current launch is recorded in the `0.1.0-tile`
> section below.

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

## Two defects the console exposed (historical: 0.1.0-fd5d0ae run, 2026-11-18 morning)

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

## P2 — boot and resources (historical: 0.1.0-fd5d0ae, 2026-11-18 morning)

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

## P3 — capabilities (historical: 0.1.0-fd5d0ae, 2026-11-18 morning)

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

## P4 — transfers (historical: 0.1.0-fd5d0ae, 2026-11-18 morning)

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

## Local agents — historical build 0.1.0-4d7499f

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

## Delegation — historical build 0.1.0-c0f76b5

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

## Skills and targeted edits — historical build 0.1.0-653d046

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

## Home-screen tile — historical precursor `0.1.0-01c6cbc`

The generated `icon0.png` was replaced with a 512x512 PS5-style tile: near-black
cool gradient, soft vignette, a white H monogram with a glass highlight and one
blue bridge. It is rendered at 2x and box filtered, so it stays legible at the
128px size used in a home row; the title is shell-rendered rather than baked in.
This was the package-art precursor (2026-11-18; exact clock time not recorded).
The install path is now implemented and verified on the console by the later
`0.1.0-tile` section above; the actual
home-screen row rendering is still unobserved from the remote session.

## Memory-safety rebuild — historical `0.1.0-e6b6205`

The parser hardening was rebuilt into a fresh payload and re-smoked on the
console without a provider key. `--tools` listed the same 14 tools. A 64-byte
PE file with `e_lfanew = 0xffffffff` returned `format: pe` and
`signature: false` instead of throwing; `fs_tree` with `depth: 1e999` returned
`depth: 8` and `truncated: true`; and a mission with `max_steps: 1e999` was
rejected with `max_steps must be between 0 and 64` before any child started.
The full fuzz and sanitizer results are in the host logs, not on the console.

## Local recon, plan, swarm and mission (historical: 0.1.0-e0b6af4)

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

## P5 — extension contract (historical: 0.1.0-fd5d0ae-era, 2026-11-18)

The `hello` plugin was built outside the package with only
`include/ps5h/plugin_api.h` and the SDK, then loaded by the console binary. The
ABI check (`host->abi_version >= 2`), tool registration and handler execution
all worked. Plugins are in-process; a plugin that crashes takes the harness with
it. That isolation choice was not tested with a crashing plugin.

## Rest mode — code evidence

The harness is an elfldr payload, not a service that starts itself. Since this
report was captured, `--serve` added an explicit long-lived LAN mode, and the
listener loop now rebinds after a suspended process resumes. It still runs only
when a human launches `harness.elf`; it is not started by rest mode. The
evidence is in the tree, not only in this report:

| Claim | Reference |
|---|---|
| The artifact is built for elfldr with a `file:` URI | `platform/ps5/Makefile:89`; `platform/ps5/BUILD.md:218` |
| The console run used that path | `docs/CONSOLE-REPORT.md`, “How it was launched — historical initial 0.1.0-fd5d0ae run” (uploaded through the web file manager, started by `elfldr`) |
| The harness process ends after one command or one agent run | `src/main.cpp:614` (entry); returns at `:1191` (mission), `:1196-1214` (`--call`), `:1217-1219` (`--tools`), `:1283-1286` (no prompt), `:1293-1295` (agent run); `--serve` blocks at `:1279` |
| The harness opens no listener by default | `src/server.cpp` binds only in the explicit `--serve` path; `src/http.cpp` remains the outbound libcurl client |
| `--serve` requires a bearer token and is opt-in | `src/server.cpp` rejects every unauthenticated `/api/*` request; `docs/SECURITY.md` invariant 15 |
| It does not start itself from rest mode | the listener starts only after a human launches the payload with `--serve`; nothing registers it as a wake source or service |
| A suspended process rebinds its listeners after resume | `src/server.cpp` `poll()` loop; triggers are `SIGCONT`, a 5-second-or-more wall-clock jump, and a change in the non-loopback IPv4 address set; host `server_test` holds the network down after `SIGCONT`, proves HTTP/UDP still answer from the old listeners, releases it, then verifies the rebind and a second resume |
| 9021 and 8888 belong to other processes | `docs/CONSOLE-REPORT.md`, Environment table (elfldr and the web file manager) |

Suspending the console stops execution. Current PS5 reports say the userland
process is frozen with its memory intact, while its listening sockets are
closed; the new listener loop handles that case. It detects `SIGCONT`, a
wall-clock jump of at least five seconds, or a change in the non-loopback IPv4
address set. It keeps still-valid HTTP, HTTPS and UDP listeners in the poll set
while the network is down, checks the network at a bounded one-second interval,
and retries bind failures with 2/4/8/16/30-second backoff. A second resume
resets the wait. If a particular firmware or loader actually terminates the
process, the harness, elfldr and the file manager are gone and must be
relaunched. No separate payload is needed for the suspend case; a
resident/kernel component would only be relevant to process termination.
Nothing in the repository registers a background service, wake source or
scheduler, so no tool call, skill, edit or model turn can execute while the
console sleeps.

The only workable path for work during sleep is an always-on host queue: keep
the jobs on a machine that stays awake, wake the console, then run the queue
through the harness. If the suspended process survived, the listener rebinds
without a relaunch; otherwise launch `harness.elf` first. This report did not suspend the console — waking
it requires a human at the machine — so the limitation is established from the
launch path and process lifetime above, not from a sleep test.

## 0.1.0-final LAN workspace (baseline)

> Historical release record (2026-11-18; exact clock time not recorded). The
> current artifact and live capture are in the `0.1.0-tile` section above; the
> hashes and sizes below are not current.

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
| `harness.elf` (historical baseline; not current) | 9,727,192 | `5e60306dcdaf6ac4cb336e1877880c57fba45a23428b20ef2c71270145a12ef8` |
| `eboot.bin` (historical baseline; not installed) | 7,733,984 | `f65de21248d82a60bfc3ba5d0f606c0de522101683697e461757656b36b2c2f4` |

`make -B` was rerun to check byte reproducibility; the PS5 linker embeds a
per-link build-id, so the hash changes between identical links. The table above
is the historical binary staged for that release, not the current runtime and
not a claim of reproducible bytes. `ps5_package_test` passed on the package.

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
`0.1.0-final` instance on 8789; this is historical and is not the current 8789
process, which is the `0.1.0-tile` PID 107 described above. The test token is
not recorded here.

### Checks run

- `make test` — exit **0** (host suite, including session, server, power tools,
  package validation and the provider e2e test).
- `PATH=/opt/homebrew/opt/llvm/bin:$PATH make -B -C platform/ps5
  BUILD_ID=0.1.0-final` — exit **0**; `ps5_package_test` passed.

## Home-screen tile — `0.1.0-tile`

The harness now writes its own home-screen source at startup instead of relying
on a pre-staged package folder. The tile is a web-app shell entry, not a copy of
the payload ELF:

```/data/homebrew/HARN00001/           0777
  eboot.bin                        0777  copied from staged /data/ps5h-web-shell.bin (original /system_ex/app/NPXS40106/eboot.bin)
  sce_sys/param.json               0777  HARN00001, IV9999-HARN00001_00-HARNESSHOMETILE1
  sce_sys/icon0.png                0777  512x512 RGBA, generated in code, 1,049,236 bytes
```

The harness payload runs as uid 1 and cannot see the `/system_ex` mount even
though the console's WFM process can. The exact source shell was therefore
downloaded through WFM and staged at `/data/ps5h-web-shell.bin`; the app uses
that staged file as `--web-shell`. Staging is required so a remove/re-add cycle
can work after the tile folder itself is deleted. The staged copy and the tile's
`eboot.bin` both match the source byte-for-byte.

The copied `eboot.bin` matched the console shell exactly (5,211,435 bytes,
sha256 `684d59bbd791dee90e891fe9d89e440ead58bf146ae53a3cedfa5a04970a9fbe`). The
generated tile icon hash is
`0cbbb5131134a10542241a3f1b260f09b5b4df0e37c378983d1c8dc878335f2e`; it is a
512x512, 8-bit RGBA PNG of 1,049,236 bytes. The current tile `param.json` is
333 bytes, sha256
`4b64bbaabf5ecdaa13c1521705a60f0aa6e74aa064d4b13e3a53099f3cf826d1`,
with `deeplinkUri` `http://127.0.0.1:8791/`.
The staged shell and the tile `eboot.bin` are both 5,211,435 bytes with that
same `684d…` hash.

At the live capture on 2026-11-18 17:00–17:14 PST, after a full power cycle, the
primary process was **PID 103** on HTTP 8791 / HTTPS 8790; a second
same-artifact instance on HTTP 8789 was **PID 107**. The signal helper was
launched through elfldr and returned `kill pid=103 sig=17 rc=0` and
`kill pid=103 sig=19 rc=0`. The measured transition was:

| Stage | HTTP 8791 | HTTPS 8790 | HTTP 8789 (PID 107) |
|---|---:|---:|---:|
| Before stop | **200** | **200** | **200** |
| After `SIGSTOP` 103 | **000** | **000** | **200** |
| After `SIGCONT` 103 | **200** | **200** | **200** |

A separate stop test on PID 107 made HTTP 8789 return **000** while HTTP 8791
stayed **200**; after `SIGCONT`, 8789 returned **200**. This identifies both
listeners by the PID that owns them. No second stop test was run. At 17:06 PST
the same boot was rechecked with `ps5h-live` over elfldr: it listed PID 103 and
PID 107, and 8791, 8790 and 8789 all returned **200**. A final non-destructive
service check at 17:14 PST again returned **200** on all three; no SIGSTOP was
repeated.

The current launch used `/data/harness.elf` through elfldr with
`--config /data/ps5h-mission.json`, `--serve 8791`, `--serve-tls 8790`, the
TLS certificate/key, `--web-shell /data/ps5h-web-shell.bin`,
`--homebrew-root /data/homebrew` and `--home-tile-autostart`. The 8789
companion used the same artifact and config with `--serve 8789`. The tile was
re-registered from PID 103 after the tests (`POST /api/home-tile`); the probe
then still returned `installed:true` / `status:0`, and the rewritten
`param.json` pointed at 8791.

The launch path is `/data/harness.elf`; the file downloaded from the console is
**9,898,288 bytes**, sha256
`f47fe099b7cb919561d2990dc67ff9c61f350a69a2397c4273c2ce8c6731e9e6`. The console
does not expose `/proc`, so provenance is the recorded launch path, the
PID/port ownership test and the matching executable file hash rather than a
separate `/proc/<pid>/exe` read or a proc-stat observation.

An earlier capture at 2026-11-18 16:18 PST recorded PID 467 on 8791/8790 and a
legacy input instance PID 302 on 8789. That SIGSTOP/SIGCONT test was performed
on PID 467, not on the current process; it is historical. The 16:26 PST outage was cleared
by the 16:59 power cycle, and the current process was relaunched at 17:00. The
current state does not require a manual wake.

Registration was checked only through the required endpoint:

```text
POST http://127.0.0.1:10101/api/v1/games/info {"title_id":"HARN00001"}
HTTP 200
{"path":"/data/homebrew/HARN00001","title_id":"HARN00001",
 "installed":true,"source_available":true,"platform":"ps4","status":0}
```

At the 17:14 PST recheck the current probe returned `installed:true`,
`source_available:true`, `platform:ps4`, `status:0`, and content ID
`IV9999-HARN00001_00-HARNESSHOMETILE1`; the deeplink in `/api/health` is
`http://127.0.0.1:8791/`. The `/api/health` response from PID 103 was:

```json
{"ok":true,"build":"0.1.0-tile","home_tile":{"checked_at":1795050254,"deeplink":"http://127.0.0.1:8791/","detail":"Registered with ShadowMountPlus.","folder_present":true,"installed":true,"library_excluded":true,"path":"/data/homebrew/HARN00001","reason":"","registered":true,"removed":false,"scan_path_ok":true,"scan_paths":[],"service_reachable":true,"shell_available":true,"state":"installed","title_id":"HARN00001","title_name":"PS5 Harness","web_shell":"/data/ps5h-web-shell.bin"}}
```

`checked_at` is 17:04 PST (when the tile was re-registered); the 17:14 recheck
reads that same installed object.

`/api/state` from the same PID 103 at that capture reported build `0.1.0-tile`,
20 tools, and the same installed/registered tile fields. A full remove/readd cycle was exercised on the console:
removal deleted the folder, left `.ps5h-home-tile-removed`, made the info route
return 404, and rescanned. The chat process was then restarted while the marker
was present; the folder stayed absent and the info route stayed 404, proving the
marker blocks startup restoration. `Add to home screen` cleared the marker,
rewrote the folder, and registration returned HTTP 200 / `status:0` after the
next scan. The startup log also recorded
`home tile: Registered with ShadowMountPlus.`

Not verified: the PS5 home row itself was not observed from this remote session,
and the tile was not tapped. Whether the shell opens the deeplink full screen is
therefore still a console-side visual check; the source, permissions and
ShadowMountPlus registration were confirmed at that capture. The exact web-shell path can differ
on another firmware, and the uid-1 payload sandbox may hide `/system_ex` there
too; this deployment uses the exact staged copy described above.

## PS5 chat presentation — `0.1.0-chat`

> Historical release record (2026-11-18; exact clock time not recorded). The page
> sizes and access measurements below are for `0.1.0-chat`, not the current
> `0.1.0-tile` build.

This section records the `0.1.0-chat` workspace. The current `0.1.0-tile` build
keeps that presentation and adds the Home screen panel; the tile evidence above
is the current live deployment.

The browser workspace now uses the PS5 system language rather than a generic
chat-app layout: a dark control-center rail, PlayStation-blue identity tile,
system-ready status, dashboard action cards, and a PS5 Messages-style
transcript. Outgoing messages are right-aligned blue bubbles; console replies
are left-aligned dark bubbles with a circular PS avatar. The footer is a single
rounded message bar with keyboard, microphone, help and circular send controls.

### Deployed listeners

| Check | Measured |
|---|---|
| `GET /` over HTTPS 8790 | **200**, 76,320 bytes |
| `GET /` over HTTP 8791 | **200**, 76,320 bytes |
| Legacy `GET /` over HTTP 8789 | **200**, 53,581 bytes (retained input build) |
| Unauthenticated `/api/state` | **401** `{"error":"unauthorized","ok":false}` |
| Authenticated build | `0.1.0-chat` |
| Registered tools | **20** |
| Saved history | **10 messages**; context **7,611 / 4,096** chars, **3 compactions** |

The old HTTPS process was identified by its individual PID and stopped with
`SIGTERM`; the legacy 8789 process was left running. The exact uploaded ELF was
started through elfldr on 8791 with TLS also on 8790, using the existing mission
config, session, certificate and key.

### Browser checks

- Headless Chromium against the deployed HTTPS origin: secure context **true**,
  no page errors, 10 saved messages, outgoing bubble right of the incoming
  bubble, 40 px circular send control, no horizontal document overflow.
- Desktop 1440×900 and phone 390×844: composer and mobile navigation do not
  overlap; the on-screen keyboard closes above the mobile navigation; the help
  sheet's top Close control is visible and closes it.
- Layout audit at 1440×900, 1280×800, 1180×800, 1024×768, 900×700, 768×1024,
  390×844, 360×640 and 320×568: no horizontal overflow, no blocked controls, and
  Activity/Tools, OSK, help, typing and chip insertion all worked.
- `make test` — exit **0** after the final UI changes.
- Cross-build `make -C platform/ps5 BUILD_ID=0.1.0-chat` — exit **0**.
- Certificate verification still reports `CSSMERR_TP_NOT_TRUSTED`; browser tests
  use the certificate-bypass profile. Warning-free access requires the user to
  set **Always Trust** on the imported `PS5 Harness` certificate in Keychain
  Access, or run the documented `security add-trusted-cert` command. This was
  not automated because macOS asks for interactive authorization.

## Input layer — `0.1.0-input`

> Historical release record (2026-11-18; exact clock time not recorded). The
> artifact hashes and measurements below are for `0.1.0-input`; they are not the
> current running artifact.

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
| `harness.elf` (historical input build; not current) | 9,759,960 | `7509335ae3300a53f52493ced21d0eab75bb51dd6b66511de1ab45fcecb1c43c` |
| `eboot.bin` (historical input build; not installed) | 7,766,816 | `d8b1a976913bf581210d66fc01b775c78e9630b49bcd00c473df8aa9b26e55bc` |

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

> Historical release record (2026-11-18; exact clock time not recorded). The
> artifact hashes, page sizes and access results below are for `0.1.0-https`; the
> current live capture is above.

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
| Retained HTTP URL at that release | `http://192.168.4.100:8789/` (historical `0.1.0-input` process; not current) |
| Certificate | `/data/ps5h-cert.pem` |
| Private key | `/data/ps5h-key.pem` |
| Certificate | self-signed local root, `CN=PS5 Harness`; SAN `192.168.4.100`, `127.0.0.1`, `localhost` |
| Certificate SHA-256 | `8C:80:7E:62:8F:B0:4B:CB:70:25:7C:6A:97:06:43:CD:96:63:24:FF:C7:CA:67:DE:79:32:49:9A:7E:BD:F7:B3` |
| `harness.elf` (historical 0.1.0-https; not current) | 9,761,576 bytes, sha256 `965d89549d80fb28e70f29cd54a8f05f5942e8d7c2d8b18485084696f3fb1d6b` |
| `eboot.bin` (historical 0.1.0-https; not installed) | 7,773,504 bytes, sha256 `3109fa9bed23e80bc08652f588f4b65eefc5078d78e34c576faecd39f95a9a82` |

With a client copy of the certificate at `/tmp/ps5h-cert.pem`, `curl --cacert
/tmp/ps5h-cert.pem https://192.168.4.100:8790/` returned **200** and the page was
53,581 bytes. The self-signed certificate must be trusted by the browser for a
warning-free normal session; the exact macOS and iOS steps are in the README’s
**HTTPS and microphone trust** section.

**Trust status on the test Mac:** the certificate was imported into
`~/Library/Keychains/login.keychain-db`, but macOS did not apply the trust
setting. `security dump-trust-settings` reports **no trust settings** and
`security verify-cert` reports **`CSSMERR_TP_NOT_TRUSTED`**. The import opened a
`SecurityAgent` authorization prompt that this automated session cannot approve.
That is the remaining user-only step: approve the prompt, or open the imported
**PS5 Harness** certificate in Keychain Access and set **Always Trust** for SSL.
The browser evidence below therefore used a test-profile certificate bypass; it
proves secure-context and microphone behavior, not genuine warning-free trust.
The TLS chain itself was validated by `curl --cacert`.

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
non-system messages** (12 total including the system prompt and summary). At the historical `0.1.0-https` release (2026-11-18; exact clock time not recorded) the legacy 8789 process was left running; its then-live check identified it as PID 302 and it still returned HTTP 200, while the chat payload
was reachable on both 8790 and 8791 at that capture.

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

## Funded provider smoke test — 2026-11-19

On the release build `0.1.0-cde3a9c`, a temporary Venice API key was staged at
`/data/ps5h-secrets/api-key` and `/data/ps5h-test-config.json` set
`model.api_key_file` to that path, `model.base_url` to
`https://api.venice.ai/api/v1`, and `model.model` to
`venice-uncensored-1-2`. The console's authenticated `/api/chat` endpoint
returned HTTP 200 with:

```json
{"answer": "console model ready", "ok": true, "usage": {"completion_tokens": 4, "prompt_tokens": 3110, "steps": 1, "total_tokens": 3114}}
```

The request used the console key-file path and made no tool calls. This was a
direct LAN HTTP turn, not a phone/browser-UI session. The temporary key and test
config were removed from the console after the run; neither is in the release
bundle. The same temporary key was then used for the five capability turns
below, and was removed again after that run.

## Funded provider capability turns — 2026-11-19

These ran on release build `0.1.0-8c4903b` through the funded Venice endpoint
`https://api.venice.ai/api/v1` from a LAN host. The test config used
`model.model = "zai-org-glm-5-1"` and `max_steps = 12`; the successful
tool-choice turn used `model.max_tokens = 2048`, then the shared pool was raised
to `16384` for the remaining turns. `/data/ps5h-secrets/api-key` held the
temporary key. The short-lived profiles were `probe-scout` (read a named file)
and `probe-mapper` (list `/data/homebrew`);
`/data/homebrew/ps5h-proof-tool.txt` contained exactly
`TOOL_CHOICE_PROOF_20261119` plus a newline. All temporary console files and
the host key were deleted afterward.

| Path | Route | Provider result | Steps | Prompt / completion / total | Tool or delegation activity |
|---|---|---|---:|---:|---|
| Tool choice | `/api/chat` | exact proof content | 2 | 5,943 / 70 / 6,013 | `tool fs_read {"path":"/data/homebrew/ps5h-proof-tool.txt"}`; tool message `TOOL_CHOICE_PROOF_20261119\n` |
| Plan | `/api/chat` with `--plan` | exact content plus read-only proposal | 2 | 5,071 / 202 / 5,273 | 12 read-only tools; `tool fs_read`; no write tool loaded |
| Delegation | `/api/chat` | child answer repeated verbatim | 2 | 6,083 / 105 / 6,188 | `tool delegate` -> `probe-scout`; child 2 steps / 5,915 tokens with `fs_read` |
| Swarm | `/api/chat` | both lanes reported | 2 | 6,263 / 246 / 6,509 | `tool swarm`, width 2; `probe-scout` 2/5,915 with `fs_read`; `probe-mapper` 2/6,296 with `fs_list` |
| Multi-step mission | CLI `--mission` | `ok: true` | 2 mission steps | `read`: 2 / 5,945; `follow`: 3 / 10,087 | `fs_read` in `read`; `fs_list` and `fs_read` in `follow`, with the first answer carried into the second task |

Exact `/api/chat` messages sent by the host:

```text
Tool choice: Use the fs_read tool on /data/homebrew/ps5h-proof-tool.txt and report the exact file content. Do not answer without calling fs_read.
Plan: In read-only plan mode, use fs_read on /data/homebrew/ps5h-proof-tool.txt and report its exact content. Then propose one read-only next step. Do not write or modify anything.
Delegation: Use the delegate tool exactly once. Call delegate with profile "probe-scout" and task "Use fs_read on /data/homebrew/ps5h-proof-tool.txt and answer with its exact content." After it returns, report the delegate answer verbatim. Do not use fs_read yourself.
Swarm: Use the swarm tool exactly once with two lanes. Lane 1: profile "probe-scout", task "Use fs_read on /data/homebrew/ps5h-proof-tool.txt and answer with its exact content." Lane 2: profile "probe-mapper", task "Use fs_list on /data/homebrew and report whether ps5h-proof-tool.txt appears." Do not inspect the files yourself; report both lane results.
```

The mission tasks are quoted in its step results above.

Observed final answers:

- **Tool choice:** `The file /data/homebrew/ps5h-proof-tool.txt contains
  exactly ... TOOL_CHOICE_PROOF_20261119 ...` The assistant history entry had a
  real `tool_calls` array, and the following tool message was the exact file
  content.
- **Plan:** the model called `fs_read` and returned the exact content, then
  proposed `file_info` as a read-only next check. The plan-mode tool list was
  `fs_list, fs_read, skill, env_info, fs_tree, file_info, fs_strings,
  fs_search, hexdump, sysctl, delegate, swarm`; the state log opened with
  `browser UI listening ... (plan/read-only mode)`.
- **Delegation:** the parent called `delegate` once with
  `profile: probe-scout`; the child made its own funded provider turn, called
  `fs_read`, and returned `TOOL_CHOICE_PROOF_20261119`. The parent answered
  with that string verbatim.
- **Swarm:** the parent called `swarm` once with two lanes. Lane 1 returned the
  exact content; lane 2 returned `ps5h-proof-tool.txt appears ... 27 bytes`.
  Both child turns logged their own token usage (`5,915` and `6,296`).
- **Mission:** the CLI logged `mission start name=provider-proof-mission
  steps=2`, then `mission step=read profile=probe-scout` and `mission
  step=follow profile=probe-mapper`. The final JSON contained both step
  results, both `ok: true`, and the exact content in both answers.

**Model selection finding.** The first funded tool-choice attempt used
`venice-uncensored-1-2`. It completed one provider turn (3,136 prompt / 21
completion tokens) but returned the intended arguments as assistant text
(`{"path": "/data/homebrew/ps5h-proof-tool.txt"}`) and no `tool_calls`; no
tool ran. A direct `/chat/completions` probe reproduced this.
`zai-org-glm-5-1` emitted a real OpenAI-style `tool_calls` entry for the same
request, so the capability run uses that model. This is a provider/model
function-calling difference, not a weakened harness test.

**Mission route limitation.** `/api/chat` cannot run a mission because
`src/main.cpp` rejects `--serve` together with `--mission` (line 1024). The
funded mission above therefore used the CLI runner. The mission is real-provider
and multi-step; a chat mission would require a separate entry point.

## Still unverified

- Tapping or launching the installed HARN00001 home-row tile. The tile folder is
  installed and registered, but home-row placement and the tap/launch were not
  observed from this remote session.
- Title install and launch of the packaged PHAR00001 `eboot.bin` (fake SELF, SFO
  keys, shell entry).
- Console firmware and exploit versions; the loader and web file manager
  versions.
- USB writes and an app-sandbox write.
- Peak RSS during a large transfer.
- Provider-backed turns through the on-console browser UI or a phone session.
  The capability run used the authenticated `/api/chat` endpoint and the CLI
  mission runner from a LAN host.
- Re-running the provider capability suite against the final docs-only release
  commit. The provider evidence above is from `0.1.0-8c4903b`, whose code is
  unchanged by the documentation commit.
- The exact CA bundle path and resolver used.
- Rest mode. The harness is a userland payload and does not execute while the
  console is suspended. The listener-rebind path is covered by a host
  `SIGCONT`/network-gate test, but this build was not tested by entering rest
  mode. A hardware run must record whether the jailbreak/process survived and
  whether HTTP, HTTPS and UDP discovery return after wake; if not, relaunch
  the payload.
