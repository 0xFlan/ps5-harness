# PS5 Harness — master plan

Status: **P0 and P1 complete; P2 and P3 are verified on a console for the elfldr
payload, and most of P4 is.** The agent boots, probes 16 CPUs / 2185 MiB /
13952 fds, reaches HTTPS, loads a plugin and writes `/data`, `/data/homebrew`
and `/user`. The packaged title has not been installed, and USB and peak-memory
measurement remain open.

| Phase | State |
|---|---|
| P0 Repo hygiene | Done — MIT license, `.gitignore`, `VERSION`, public GitHub repository and CI green on Ubuntu and macOS. |
| P1 Core hardening | Done — A1–A7 closed with tests, A8 partial. Suites pass under ASan/UBSan; 20k fuzz iterations clean, including the newer recon and mission surfaces; macOS leak check clean. |
| P2 Console bring-up | Partial — `harness.elf` boots under elfldr and logs 16 CPUs, 2185 MiB, 13952 fds, 342 GiB free on `/data` and a 16-worker budget. The packaged `eboot.bin` title has not been installed, so the phase's literal completion is not met. |
| P3 Capabilities | Done for the payload path — TLS (200, bad cert rejected), `dlopen` and writes to `/data`, `/data/homebrew` and `/user` observed 2026-11-18; USB was absent. |
| P4 Tool surface | Mostly done — resume returned 206 with a matching digest and the free-space cap fired on a 2 MiB mount; 1 GB and peak RSS tracked below. |
| P5 Extension ecosystem | Partial — ABI v2 frozen, manifest compiler and example plugin exist; registry not built. |
| P6 UX | Partial — opt-in LAN browser workspace with persistent sessions, live activity/tool panels, compaction and a headless-browser reconnect test is shipped and host-tested; not yet exercised from the PS5 runtime. Native on-console UI remains open. |
| P7 Audit + release | Partial — threat model, fuzz, ASan and the provider round trip done; hardware audit and release engineering pending. |

| Document | Purpose |
|---|---|
| `docs/AUDIT.md` | Confirmed defects in the current code, with fix specs |
| `docs/SECURITY.md` | Threat model, invariants, secret handling |
| `docs/HARDWARE-CHECKLIST.md` | Console smoke sheet, filled in per attempt |
| `docs/RECOVERY.md` | Connection loss, rest mode, re-jailbreak and address/port recovery |
| `MISSION.md` | Operating rules to keep this plan on the rails |

## What exists today

`ps5-harness` builds on macOS with `-Wall -Wextra` and no warnings. `make test`
runs the host suites plus the PS5 package validator. Built-in tools
(`fs_list`, `fs_read`, `fs_write`, `fs_edit`, `http_get`, `download`, `skill`,
the four read-only recon tools, `delegate` and `swarm`), manifest compilation
(`examples/endpoints/archive.json` → 3 tools), and a `.so` plugin loading
through the C ABI all work. `--plan` builds a read-only registry; JSON
missions compose sequential profile turns and bounded swarms; six
clean-room text skill packs are compiled in. One live HTTPS call to
`archive.org/advancedsearch.php` returned real records. Local agent profiles
(`--agent`, `--list-agents`, `--agent-info`) load prompt and model overrides
from JSON files on disk; editing a profile changes the next run without a
rebuild. A running agent can call `delegate` to hand a bounded subtask to
another profile; the child inherits the parent's tools and security policy and
charges its steps/tokens to the parent's budget. Skills are local `SKILL.md`
packs listed and loaded through a path-confined tool; `fs_edit` makes a targeted
replacement instead of rewriting a whole file. Both are built-ins, so every
profile and delegated child receives them under the same main-config roots. The
runtime derives its worker
count, response buffers and download capacity from a measured resource profile
rather than host constants.

That is a proof of the extension contract, not a product. The host defects in
`docs/AUDIT.md` are fixed, and the console artifact now boots and runs on a PS5
(2026-11-18, evidence in `docs/CONSOLE-REPORT.md`).

## Sequencing rule

Phases are gated. A phase starts only when its predecessor's completion
criteria are met and recorded here. Security work (P1) precedes console work
(P2) deliberately: path escapes and unbounded buffering are cheaper to fix on a
host where they can be tested, and they become memory-corruption or OOM bugs on
a console with no debugger.

## P0 — Repo hygiene — COMPLETE

Delivered: `LICENSE` (MIT), `.gitignore`, `VERSION` (0.1.0), public GitHub
repository at `github.com/0xFlan/ps5-harness`, and CI at
`.github/workflows/ci.yml` (Ubuntu + macOS: build, full test including the
browser reconnect runtime test, plugin smoke). CI is green on both runners.

Deliverables: `git init`, `LICENSE` (MIT), `.gitignore` (`build/`, `*.so`,
`*.dylib`, `.deps/`, `dist/`), CI on ubuntu-latest and macos-latest running
`make && make test`, version `0.1.0` in a single `VERSION` file read by the
Makefile.

Completion: CI green on both runners; `make clean && make test` from a fresh
clone; `README.md` build section matches reality.

## P1 — Core hardening (host only) — COMPLETE

All of A1–A7 closed with proving tests; A8 partial (CA-bundle/clock policy for
console TLS remains open). New
modules: `paths`, `policy`, `secrets`, `sha256`; streaming download client;
ASan/UBSan-clean suites; fuzz drivers for the JSON parser, manifest compiler,
recon header/string/tree surface and mission parser, plus a macOS leak check.

Fix A1–A4, add streaming HTTP, path canonicalisation, manifest URL policy,
per-tool deadlines, and a fuzz target. Full specs in `docs/AUDIT.md`.

Order matters: A3 (streaming) changes the HTTP interface that A4's tests use,
so do A3 first, then A1/A2 (filesystem), then A4 (network policy), then A5/A6.

Completion: every audit fix has a test that fails before it and passes after;
ASan+UBSan clean on the test suite; fuzz corpus runs 10 minutes without a crash
or a new hang; `make test` still green on macOS and Linux.

## P2 — Console bring-up

Decide the toolchain path first (record it in the decisions log below): Docker
Linux container on this Mac, a Linux VM, or a remote builder. Then: payload SDK,
`libcurl` from PacBrew's pinned ports, file logging, a crash handler that writes
to USB, `sce_sys/` assets, and a real `param.json` with a unique title ID.

Completion: `eboot.bin` boots on the console and writes a log line containing
its build id and the console's free space. Nothing else is required of it yet.

State: the toolchain decision is recorded — macOS host, SDK zip sha256
`5fac6409…` (v0.45), LLVM 23.1.4, and PacBrew v0.41.1 (bundle sha256
`e811bc2e…`) for static libcurl 8.18.0 / OpenSSL 3.5.2 (details in
`platform/ps5/BUILD.md`). `make -C platform/ps5` compiles every `src/*.cpp` plus
`platform/ps5/console_compat.cpp`, links the PacBrew archives, and packs a
fake-SELF `eboot.bin` with the SDK's `make_fself.py`. The package holds
`eboot.bin`, `harness.elf`, `sce_sys/param.sfo` and `sce_sys/icon0.png` (build
`0.1.0-fd5d0ae`). `harness.elf` is a PIE/ET_DYN link for elfldr; `eboot.bin` is
packed from a separate non-PIE link because `make_fself.py` rejects ET_DYN. The
title ID is chosen once in `platform/ps5/title.mk`
(`PHAR00001`, homebrew range) and appears in the SFO, `param.json`, the package
folder and the agent log.

`console_compat.cpp` supplies the console CA bundle path, `SO_NBIO` socket mode
and an `fcntl` wrapper for sandboxed sockets. The SDK's payload libc already
provides the resolver and libc symbols that native titles needed to stub; the
link resolves without redefining them. `param.sfo` is generated as a real SFO
binary by `platform/ps5/tools/make_sfo.py`; `icon0.png` is a real 512x512 RGBA
PNG from `platform/ps5/tools/make_icon.py`. `make test` regenerates both and
runs `build/ps5_package_test`, which parses the SFO and PNG from disk and checks
the fSELF magic and agent ELF header.

The runtime now consumes the probe: workers equal online CPUs unless
`RLIMIT_NOFILE` cannot support them, buffered responses are a sixteenth of
visible memory capped by `RLIMIT_AS`/`RLIMIT_DATA`, and download capacity is
the destination's free space minus a reserve and `RLIMIT_FSIZE`. `runtime_test` and the parallel tool-call e2e case
fail if those stop following the measurements. The agent logs build id, resource
lines and budget to `/data/ps5-harness-agent.log` (with fallbacks) and accepts
`--log`.

Boot and the resource log are now observed: `harness.elf` started under elfldr
at build `0.1.0-fd5d0ae`, wrote `/data/ps5-harness-agent.log` with 16 online
CPUs, 2185 MiB visible memory, `nofile 13952/13952`, 342 GiB free on `/data`,
and `workers=16 buffered=136 MiB file-read=4 MiB`, then exited cleanly. A title
launch still supplies no argv or environment, so the packaged `eboot.bin` has
not been installed or run; the `file:` URI with `?args=` is the runnable path.
The full transcript is in `docs/CONSOLE-REPORT.md`.

## P3 — Console capabilities

Three separate proofs, each recorded in the hardware checklist:

1. **TLS** — an HTTPS GET to a known host returns 200, with the clock verified
   first. If it fails, the diagnosis order is: clock skew → CA bundle → TLS
   version, not "the network is broken".
2. **`dlopen`** — load the example plugin `.so` and see its tool appear.
3. **Elevated write** — write one byte under `/data/homebrew`, then one byte to
   a USB path, reporting which of the two worked.

Completion: all three demonstrated on hardware. A partial result is a valid
outcome and changes the design: no `dlopen` means manifests-only plus a
helper-process plugin host; no elevated write means USB-first storage.

TLS, `dlopen` and elevated writes to console paths are now observed from the
elfldr payload path (`docs/CONSOLE-REPORT.md`): `https://example.com/` returned
200 while an expired certificate was rejected; the SDK-built `hello.so` loaded
and ran; and writes succeeded under `/data`, `/data/homebrew` and `/user`.
USB was absent, so that half of the elevated-write check is open, and none of
this has been repeated inside a title sandbox.

## P4 — Tool surface

Streaming downloader: Range resume, sha256 verification, atomic rename from
`.part`, free-space precheck, cancellation. Install to `/data/homebrew` and USB.
Title list and launch, behind the elevation capability probe from P3.

The measured budget is now implemented: worker concurrency from `cpu_online`
bounded by the fd limit, buffered responses from a sixteenth of visible memory,
and download capacity from the destination filesystem and `RLIMIT_FSIZE`.
Streaming downloads and per-tool deadlines still hold.

On hardware, a 10 MiB transfer seeded with a 1 MiB `.part` resumed with a Range
request (`206`), finished at exactly 10,485,760 bytes and matched its sha256. A
1 GiB OVH transfer seeded with 1,000 MiB resumed with `206`, reached exactly
1,073,741,824 bytes, matched its expected sha256 in the agent and renamed into
place; the earlier finalize crash is the 64 KiB stack buffer that `fd5d0ae`
fixed. A 10 MB request to the 2 MiB `/mnt` tmpfs stopped at 1.71 MB with the
configured-limit error. Peak RSS is still unmeasured. The remaining P4 work is
install/launch, which needs the title path.

Completion: a 1 GB transfer completes and verifies on hardware, and an
interrupted transfer resumes rather than restarting. The 1 GiB run resumed at
1,000 MiB and finalized with `206`, a matching digest and a rename; the 10 MiB
run resumed at 1 MiB on the same path.

## P5 — Extension ecosystem

Registry repo of manifests with an index file, local cache with ETags, and two
trust tiers (manifests: data; plugins: code). Plugin SDK: the ABI header, a
template repo, and conformance tests that a third party can run against their
own plugin. Freeze ABI v1 before publishing.

Decide in-process vs helper-process plugin isolation. In-process is simpler and
matches how the console loads modules; helper-process survives plugin crashes
and is the fallback if P3 shows `dlopen` is fragile.

Completion: someone who has never read this repo adds a working tool using only
`docs/` and the template, without asking a question.

### Local agents

The local counterpart to the extension registry is a profile directory. Each
`<name>.json` can set a system prompt (inline or from a file), model,
temperature, token cap and step budget; the profile is read on every launch, so
editing it on the console needs no rebuild. Profiles cannot change roots, URL
policy, secrets, endpoint/plugin directories or the delegation depth — those
remain in the main config. `--list-agents` and `--agent-info` work without a
provider key, and the host e2e suite asserts that a profile prompt and model
reach the provider request. Console-verified on `0.1.0-4d7499f`: `--list-agents`
read profiles from `/data/homebrew`, `--agent-info` resolved both an inline
prompt and a `system_prompt_file`, and editing a profile changed the next run
without a rebuild.

Skills are the other side of the local extension surface: a directory
`<skills_dir>/<name>/SKILL.md` with optional frontmatter and bundled files. The
`skill` tool lists packs, loads instructions and reads a resource; paths are
canonicalised, confined to that skill and denied by the secret-name rules. A
skill registers no tool and cannot change roots, URL policy, secrets,
`skills_dir` or the delegation depth. Profiles and delegated children get the
same tools. Console-verified on `0.1.0-653d046`: two staged packs listed and
loaded, a bundled resource read back, a parent-directory resource was refused,
and `fs_edit` changed one unique match while refusing an ambiguous one and a
write outside the roots. Six packs are also compiled in:
`reverse-engineer-anything`, `local-architect`, `astrik`, `artex`, `strix` and
`cyber-strike-ai`. They are text-only; the ARTEX/Strix/CyberStrike names are
methodology shorthands, not vendored upstream code.

The delegation extension exposes a `delegate(profile, task)` tool. The child
receives a copy of the parent's tools, so the parent's `PathGuard`, URL policy,
secrets and endpoint/plugin surface still apply; a profile that tries to set a
boundary key is rejected before the child runs. `delegation_max_depth` (default
2, `--max-depth`) bounds nesting, the child reserves steps/tokens from the
parent's pool, and refusals are logged. Host e2e covers a full parent/child
provider round trip, inherited-root refusal, depth and budget limits, flag
precedence and unknown/self profiles. Console-verified on `0.1.0-c0f76b5`: the
build id, `delegate` in `--tools`, both profile commands, and a missing-profile
refusal; no provider key was staged for an on-console child turn.

### Local recon, plan mode, swarms and missions

The recon surface is four read-only tools: `env_info` (build, platform, roots,
resources, filtered system mounts), `fs_tree` (bounded, symlinks reported not
followed, secret names denied), `file_info` (ELF/Mach-O/PE headers and sha256)
and `fs_strings` (bounded). They are path-guarded and never write or execute a
target. `recon_test` covers the formats, caps, symlinks and mount filtering.

`--plan` removes the mutating tools from the registry and appends a read-only
planning instruction; endpoint and plugin directories are not loaded. The
`swarm` tool runs up to eight orthogonal children in parallel under the same
delegation rules. `--mission <file>` runs up to 32 sequential steps, each a
profile turn or a swarm, with per-step budgets and prior answers as clipped
context; it stops at the first failure. Host e2e proves plan mode cannot create
a file, a mission runs a single step and a two-lane swarm with context, and a
nine-lane swarm is refused before any child starts. On `0.1.0-e0b6af4` the
console listed 14 tools, refused a plan-mode `fs_write` and created nothing,
returned the ELF identity and sha256 of the uploaded harness, loaded the
built-in packs, refused a nine-lane swarm, and ran a one-step mission to a
missing-profile failure. No provider key was staged for a model turn. The
current tree adds six power tools and a 12-tool plan-mode registry; the host
suite covers them, but the console has not run the new surface yet.

## P6 — User experience

The first surface is shipped: `--serve <port>` starts a token-auth LAN workspace
with a dark three-panel chat UI, persistent transcript, live activity and tool
catalogue, context/compaction/token counters, and a single-turn lock. It is
host-tested and cross-built for the payload; it has not been exercised from a
PS5 browser or used to complete a model turn on hardware. The native on-console
UI in the ProsperoStore mold remains a separate project and still needs input,
focus, text entry and streaming rendering.

The recovery surface now covers connection loss, restarts and address/port
changes: the browser retries with a timeout and exposes a new-address field; a
running primary listener publishes `/.well-known/ps5h` and answers UDP
discovery on 8799; `tools/ps5h-discover.py` finds the new origin; and
`--serve-token` plus `--session` make a relaunch reuse the token and transcript.
The host tests cover the discovery document and UDP responder; a loopback smoke
run exercised the helper over UDP and TCP. The listener supervisor also rebinds
HTTP, HTTPS and discovery after a suspended process resumes. The host coverage
is a `SIGCONT` test with a network gate that proves requests keep working while
the network is unavailable, then verifies the rebind and a second wake; the
TLS rebind is covered by `make reconnect-test`. This has not been exercised
through an actual console rest-mode wake. A firmware that terminates the
payload or resets the jailbreak still requires a manual relaunch. The support
procedure is [`RECOVERY.md`](RECOVERY.md).

Completion: a ten-minute session with no terminal open — browse a catalogue,
download something, see progress, cancel one, find the file afterwards — on the
console, using the browser workspace or a native renderer.

## P7 — Audit and release

Full threat-model pass against `docs/SECURITY.md`, ASan/UBSan, explicit
secret-exfiltration and prompt-injection tests, dependency CVE review, then
release engineering: `SHA256SUMS`, the release zip shaped as
`<TITLE_ID>/` with `eboot.bin` and `sce_sys/`, a `contentVersion` bump, and the
catalog PR. `make release` now builds the self-hosted bundle and its checksum;
the catalog submission and the hardware release checklist remain outstanding.

Completion: the submission checklist in `docs/SECURITY.md` is entirely green,
and the support matrix names the exact firmware and exploit toolchain tested.

## Decisions log

| # | Decision | Why | Revisit when |
|---|---|---|---|
| D1 | Native on-console harness, not host + bridge | Plugin extensibility is the product; the console is where the tools live | Only if P3 shows `dlopen` is unusable |
| D2 | Two extension tiers: manifests and C-ABI plugins | Most contributors should never compile anything; some need code | After P5, if manifests cover >90% of contributions |
| D3 | MIT for core, permissive ABI header | Catalog requires redistribution rights; plugins stay independently licensed | On legal review before release |
| D4 | Headless CLI first, UI later | The loop and tool surface are the risk; UI is not | P6 |
| D5 | USB-first storage | Console sandbox confinement is a known unknown until P3 | P3 results |
| D6 | Freeze plugin ABI v1 at P5 | Silent ABI churn breaks every third-party plugin at once | Major version bump only |
| D7 | No ROM/torrent/Vimm adapters, ever | Legal exposure and immediate catalog rejection | Never |
| D8 | Plugin results use an `emit()` callback into host memory (ABI v2) | v1 freed plugin-allocated memory in the host; allocator mismatch is silent heap corruption | Never (append-only from here) |
| D9 | Fuzz targets build a libFuzzer-free driver by default | Apple clang ships no libFuzzer runtime; CI must fuzz on both runners | If CI moves to Linux-only |
| D10 | Static PacBrew libcurl/OpenSSL + console glue for the agent | The payload SDK libc supplies the resolver/libc symbols; the console still needs the CA path, `SO_NBIO` and the `fcntl` wrapper | If a firmware needs a different TLS backend |
| D11 | Runtime budget derived from the resource probe | Fixed host caps left console cores and memory idle; the probe gives the real envelope | If a measured backend constraint disappears |
| D12 | Built-in skills are compiled-in first-party text | A fresh console gets RE/local-recon method without staging an unverifiable third-party tree; disk packs still override | If a signed pack format is added |
| D13 | Plan mode is a registry filter, not a prompt request | A prompt can be ignored by a model or injected content; absent tools cannot be called | Never weaken; add a new tool only with a read-only implementation |

## Open questions (answer before the phase that needs them)

- **P2:** how does a title launch supply `--config`, `--prompt` and the model
  key? A title has no argv or environment. The `file:` URI on elfldr with
  `?args=` works and is the current path; a default entry point or a config
  file for the title is still undecided.
- **P3:** answered for the elfldr payload — `dlopen`, the resolver and TLS all
  worked. Still open: whether a title sandbox permits the same app-local `.so`.
- **P4:** answered — the resource probe reports free/total space for `/data`
  and the `/mnt` tmpfs without elevation; `/user/homebrew` was unavailable.
- **P5:** registry hosted where — GitHub repo, or the harness fetches a static
  index from any URL the user configures?
- **P6:** is there a usable text-entry path on the console, or does the LAN
  surface have to carry input?

## Reporting rule

Every phase report separates three things: verified on host, verified on
hardware, and assumed. Anything in the third bucket is a risk, not a result.
