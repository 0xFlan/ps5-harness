# PS5 Harness — master plan

Status: **P0 and P1 complete; P2's artifact builds on the host.** Boot is
blocked on a console, so P3 onward has not started. Nothing has run on hardware.

| Phase | State |
|---|---|
| P0 Repo hygiene | Done — license, `.gitignore`, `VERSION`, CI workflow, first commit. CI has not executed on a remote. |
| P1 Core hardening | Done — A1–A7 closed with tests, A8 partial. Suites pass under ASan/UBSan; 20k fuzz iterations clean. |
| P2 Console bring-up | Partial — `make -C platform/ps5` emits an installable `PHAR00001` package (eboot, ELF, real `param.sfo`, real `icon0.png`) and `make test` validates it on the host; boot is blocked on a console. |
| P3 Capabilities | **Blocked** — hardware only (TLS, `dlopen`, elevated write). |
| P4 Tool surface | Partial — streaming downloader with resume and sha256 exists and is unit-tested; install/launch need P3. |
| P5 Extension ecosystem | Partial — ABI v2 frozen, manifest compiler and example plugin exist; registry not built. |
| P6 UX | Not started. |
| P7 Audit + release | Partial — threat model, fuzz, ASan and the provider round trip done; hardware audit and release engineering pending. |

| Document | Purpose |
|---|---|
| `docs/AUDIT.md` | Confirmed defects in the current code, with fix specs |
| `docs/SECURITY.md` | Threat model, invariants, secret handling |
| `docs/HARDWARE-CHECKLIST.md` | Console smoke sheet, filled in per attempt |
| `MISSION.md` | Operating rules to keep this plan on the rails |

## What exists today

`ps5-harness` builds on macOS with `-Wall -Wextra` and no warnings. `make test`
runs four host suites. Four built-in tools (`fs_list`, `fs_read`, `fs_write`,
`http_get`), manifest compilation (`examples/endpoints/archive.json` → 3 tools),
and a `.so` plugin loading through the C ABI all work. One live HTTPS call to
`archive.org/advancedsearch.php` returned real records.

That is a proof of the extension contract, not a product. The host defects in
`docs/AUDIT.md` are fixed; the console artifact builds but has never booted.

## Sequencing rule

Phases are gated. A phase starts only when its predecessor's completion
criteria are met and recorded here. Security work (P1) precedes console work
(P2) deliberately: path escapes and unbounded buffering are cheaper to fix on a
host where they can be tested, and they become memory-corruption or OOM bugs on
a console with no debugger.

## P0 — Repo hygiene — COMPLETE

Delivered: `LICENSE` (MIT), `.gitignore`, `VERSION` (0.1.0), CI workflow at
`.github/workflows/ci.yml` (ubuntu + macOS: build, test, plugin smoke), first
commit. CI has not been executed because no remote exists yet; that is the one
open item.

Deliverables: `git init`, `LICENSE` (MIT), `.gitignore` (`build/`, `*.so`,
`*.dylib`, `.deps/`, `dist/`), CI on ubuntu-latest and macos-latest running
`make && make test`, version `0.1.0` in a single `VERSION` file read by the
Makefile.

Completion: CI green on both runners; `make clean && make test` from a fresh
clone; `README.md` build section matches reality.

## P1 — Core hardening (host only) — COMPLETE

All of A1–A7 closed with proving tests; A8 partial (CI unexecuted). New
modules: `paths`, `policy`, `secrets`, `sha256`; streaming download client;
ASan/UBSan-clean suites; fuzz drivers for the JSON parser and manifest
compiler.

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
`5fac6409…` (v0.45), LLVM 23.1.4 (details in `platform/ps5/BUILD.md`).
`make -C platform/ps5` builds `platform/ps5/bringup.cpp` to an x86-64 FreeBSD
ELF and packs a fake-SELF `eboot.bin` with the SDK's `make_fself.py`. The build
now emits one installable title folder, `.deps/ps5-build/PHAR00001/`, holding
`eboot.bin`, `bringup.elf`, `sce_sys/param.sfo` and `sce_sys/icon0.png`
(build `0.1.0-56c6056`). The title ID is chosen once in
`platform/ps5/title.mk` (`PHAR00001`, homebrew range) and appears in the SFO,
`param.json`, the package folder and the payload's `title=` log field.

`param.sfo` is generated as a real SFO binary by `platform/ps5/tools/make_sfo.py`
(the SDK has no writer); `icon0.png` is a real 512x512 RGBA PNG from
`platform/ps5/tools/make_icon.py`. `make test` regenerates both and runs
`build/ps5_package_test`, which parses the SFO back from disk (magic, version,
entry table, required keys) and walks the PNG chunks and CRCs; with the SDK
build present it also checks the fSELF and ELF headers. Negative cases (missing
icon, corrupt SFO magic, truncated PNG, missing eboot) each exit 1.

The payload logs through a pre-opened fd with an app-folder fallback, records
pid/uid/cwd/argv[0], and installs an async-signal-safe fatal handler so a crash
writes its signal and address. The remaining criterion — boot and log free space
on the console — needs hardware; no part of this has run on a PS5.

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

## P4 — Tool surface

Streaming downloader: Range resume, sha256 verification, atomic rename from
`.part`, free-space precheck, cancellation. Install to `/data/homebrew` and USB.
Title list and launch, behind the elevation capability probe from P3.

Completion: a 1 GB download interrupted at 50% resumes and verifies on hardware.

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

## P6 — User experience

Choose one surface first: a LAN HTTP API driven from a phone, or an on-console
UI in the ProsperoStore mold. The LAN surface is a week; the native UI is a
month and needs input, focus, text entry and streaming rendering.

Completion: a ten-minute session with no terminal open — browse a catalogue,
download something, see progress, cancel one, find the file afterwards.

## P7 — Audit and release

Full threat-model pass against `docs/SECURITY.md`, ASan/UBSan, explicit
secret-exfiltration and prompt-injection tests, dependency CVE review, then
release engineering: `SHA256SUMS`, the release zip shaped as
`<TITLE_ID>/` with `eboot.bin` and `sce_sys/`, a `contentVersion` bump, and the
catalog PR.

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

## Open questions (answer before the phase that needs them)

- **P2:** which Linux path — container, VM, or remote builder? Needs a decision
  recorded with the exact image/version used.
- **P3:** does the target firmware permit `dlopen` of an app-local `.so`?
- **P4:** does the console expose free space per mount without elevation?
- **P5:** registry hosted where — GitHub repo, or the harness fetches a static
  index from any URL the user configures?
- **P6:** is there a usable text-entry path on the console, or does the LAN
  surface have to carry input?

## Reporting rule

Every phase report separates three things: verified on host, verified on
hardware, and assumed. Anything in the third bucket is a risk, not a result.
