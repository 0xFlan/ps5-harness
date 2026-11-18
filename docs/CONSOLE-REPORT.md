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
| Date | 2026-11-18, ≈09:18–10:10 local |
| Host | macOS 26.6.2 (arm64), LLVM 23.1.4 |
| SDK | ps5-payload-sdk v0.45 (zip sha256 `5fac6409…`) |
| Libraries | PacBrew v0.41.1 (bundle sha256 `e811bc2e…`), libcurl 8.18.0, OpenSSL 3.5.2 |
| Agent build | `0.1.0-fd5d0ae` (P2/P3 and 1 GiB); `0.1.0-4d7499f` (local agents); `0.1.0-c0f76b5` (delegation); `0.1.0-653d046` (skills, `fs_edit`) |

Current artifacts on disk (skills/edit build `0.1.0-653d046`):

| File | Bytes | sha256 |
|---|---|---|
| `eboot.bin` | 7,344,816 | `c8954690f17045f6347149a337ae995a396f0c415a7d498bb12acfbf573b172b` |
| `harness.elf` | 9,261,680 | `6ae4672c2bdf05ecb5683e2a5a62f5030e4278159828745aa6a335ed94eb3049` |
| `sce_sys/param.sfo` | 376 | `6bf5d47549c9314c6cf5b5355cae17f1603c210ff6aaafa52978221c6e19803f` |
| `sce_sys/icon0.png` | 4,223 | `38c53aee2109dc4b3200b98dcacf8a294388b84436c97298933abb0947ebbef8` |

The P2/P3 and 1 GiB evidence below was collected on `0.1.0-fd5d0ae`; the
local-agent checks use `0.1.0-4d7499f`; the delegation checks use
`0.1.0-c0f76b5`; the skills and edit checks use `0.1.0-653d046`.

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

## P5 — extension contract

The `hello` plugin was built outside the package with only
`include/ps5h/plugin_api.h` and the SDK, then loaded by the console binary. The
ABI check (`host->abi_version >= 2`), tool registration and handler execution
all worked. Plugins are in-process; a plugin that crashes takes the harness with
it. That isolation choice was not tested with a crashing plugin.

## Rest mode — code evidence

The harness is a one-shot elfldr payload, not a service that can outlive the
console's userland. The evidence is in the tree, not only in this report:

| Claim | Reference |
|---|---|
| The artifact is built for elfldr with a `file:` URI | `platform/ps5/Makefile:89`; `platform/ps5/BUILD.md:197` |
| The console run used that path | `docs/CONSOLE-REPORT.md:36` (uploaded through the web file manager, started by `elfldr`) |
| The harness process ends after one command or one agent run | `src/main.cpp:539` (entry); returns at `:955` (`--call`), `:960` (`--tools`), `:966` (no prompt) and `:974` (agent run) |
| The harness opens no listening socket | no `bind(`, `listen(` or `accept(` in `src/`; the only matches are test stubs (`tests/hardening_test.cpp:63`, `tests/e2e_agent_test.cpp:119`) |
| Its HTTP path is a client, not a server | `src/http.cpp:154` (`make_http_client`, libcurl easy interface) |
| No server mode is shipped | `docs/SECURITY.md:27` ("No listening service by default; auth if `--serve` is added"); `--serve` is only a roadmap item |
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

## Still unverified

- Title install and launch of `eboot.bin` (fake SELF, SFO keys, shell entry).
- Console firmware and exploit versions; the loader and web file manager
  versions.
- USB writes and an app-sandbox write.
- Peak RSS during a large transfer.
- A real model turn on hardware; no provider key was available to the loader.
- A delegated parent→child model turn on hardware; the `delegate` tool and its
  fail-closed path are console-verified, but no child provider call was made.
- The exact CA bundle path and resolver used.
- Rest mode. The harness is a userland payload; it has no path to keep running
  while the console is suspended, and this build was not tested by entering
  rest mode. Wake the console and relaunch the payload.
