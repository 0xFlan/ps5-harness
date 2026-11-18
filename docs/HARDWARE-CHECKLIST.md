# Hardware checklist

Run once per attempt. Copy the block at the bottom into a report and fill it in.
A phase is not complete because a build succeeded elsewhere; it is complete when
the relevant rows below are ticked on the console.

The 2026-11-18 attempt (build `0.1.0-fd5d0ae`, elfldr payload path) is recorded
in `docs/CONSOLE-REPORT.md`: P2 boot/resources, P3 TLS/`dlopen`/writes and the
P4 resume/free-space checks are filled there; the title-install and USB rows
remain open.

## Record first

- Console model and region:
- Firmware version (exact, e.g. `04.03`):
- Exploit / payload loader and version (etaHEN, kstuff, BD-JB, …):
- Date and approximate time:
- USB: filesystem, free space:

Firmware and exploit version are the two fields that make a bug report useful.
Without them a failure cannot be reproduced.

## Pre-flight

- [ ] Known-good state: the console boots and launches a retail title normally.
- [ ] USB stick formatted and empty; nothing important on it, since the harness
      will write to it during P3/P4.
- [ ] Network reachable from the console; note whether it uses DHCP or a static
      address.
- [ ] Console clock is correct (TLS depends on it; check before debugging TLS).
- [ ] Build artifact's SHA256 recorded next to the firmware version.
- [ ] Recovery known: how to uninstall the title or remove the folder if it
      misbehaves.

## Build and stage

The package is `.deps/ps5-build/PHAR00001/` and contains the real agent
(`eboot.bin` built from `harness.elf`); `platform/ps5/BUILD.md` lists the
hashes, dependency pins, console glue and exact commands.

- [ ] Host OS, LLVM version and payload SDK version recorded.
- [ ] PacBrew provenance recorded (v0.41.1 bundle sha256, libcurl 8.18.0,
      OpenSSL 3.5.2).
- [ ] Package contains `eboot.bin`, `harness.elf`, `sce_sys/param.sfo` and
      `sce_sys/icon0.png`; `make -C platform/ps5 bringup` is diagnostic only.
- [ ] `make test` passed and its `ps5 package:` lines recorded (host check, not
      a boot).
- [ ] `TITLE_ID` (default `PHAR00001`) is unique on this console, or changed in
      `platform/ps5/title.mk` and rebuilt; `param.sfo` and the folder agree.
- [ ] Artifact staged to the console by whatever method the loader expects:
      elfldr on 9021 (`nc -w 30 <ip> 9021 < …/harness.elf`) or the FTP staging
      sequence in `BUILD.md`. Record which was used, and whether argv/config/key
      were supplied.

## P2 — Boot

P2 is incomplete until every row here is ticked on hardware. A host build and a
passing package check do not satisfy any of them.

- [ ] Title appears in the shell with the expected name and icon.
- [ ] Launches without a system error dialog.
- [ ] Writes a log line containing build id and free space.
- [ ] Log records `cpu online=… configured=…`, `mem total=… available=…`,
      `limit nofile=…`, and one `mount … free=… total=…` line per filesystem,
      plus the four `candidate` lines.
- [ ] `budget workers=… cpu=… buffered=… file-read=…` is present. Compare
      `workers` with the reported online CPUs; a lower value is only acceptable
      when the fd limit explains it, and the log records the limit.
- [ ] Record the `raised RLIMIT_NOFILE …` line, if any.
- [ ] Exits cleanly (no hang on quit).
- [ ] If it did not exit cleanly: the log ends with a `CRASH build=… signal=N
      (NAME) addr=0x…` line; record it verbatim, it names the fault.

Log path (defaults, in order): `/data/ps5-harness-agent.log`,
`/user/homebrew/ps5-harness-agent.log`, `./ps5-harness-agent.log`, or the path
passed with `--log`. Record which one was written and whether a title launch
received any arguments.

## P3 — Capabilities (each independent; partial results are valid)

### TLS
- [ ] Clock verified as correct immediately before this test.
- [ ] DNS resolves from the console (record the resolver used or the exact
      `getaddrinfo`/curl error).
- [ ] CA bundle found at the sandbox path or `/system/common/cert/CA_LIST.cer`.
- [ ] HTTPS GET to a known host returns 200.
- [ ] Certificate validation enabled (no `CURLOPT_SSL_VERIFYPEER 0` anywhere).
- [ ] If it failed: which of clock / CA bundle / TLS version was the cause —
      state the evidence, not the guess.

### `dlopen`
- [ ] Example plugin `.so` present beside the app.
- [ ] Plugin loads; its tool appears in the tool list.
- [ ] Tool runs and returns a result.
- [ ] If it failed: exact `dlerror()` text recorded.

### Writes
- [ ] Write inside the app's own folder: works / denied.
- [ ] Write under `/data/homebrew`: works / denied / needs elevation.
- [ ] Write to USB: works / denied; mount path recorded.
- [ ] Which elevation path was used, if any, and whether it interfered with the
      running exploit (watch for the "no permission to write" class of failure
      other homebrew has hit after elevation changes).

## P4 — Transfers

- [ ] Small file (<10 MB) downloads and verifies.
- [ ] 1 GB download completes; sha256 matches the source.
- [ ] Interrupt at ~50%, restart: resumes from the byte offset (Range request
      observed), does not restart from zero.
- [ ] Free-space precheck refuses a download larger than the target mount.
- [ ] Cancellation mid-download leaves no `.part` orphan after cleanup.
- [ ] Peak memory during the 1 GB transfer stays flat (no growth proportional to
      file size). Record the observed figure.

## P5 — Extensibility

- [ ] A plugin built outside this repo loads unmodified using only the SDK
      header and docs.
- [ ] A manifest written from the docs alone produces a working tool.
- [ ] A plugin that crashes or hangs does not take the harness with it — record
      which isolation strategy was in place.

## P6 — Session

- [ ] Ten minutes of use with no terminal: browse, download, cancel, locate.
- [ ] Text entry is possible without a keyboard (or the LAN surface carried it).
- [ ] Output renders without clipping on a TV at normal viewing distance.

## P7 — Release

- [ ] `SHA256SUMS` verified after download from the release page.
- [ ] Fresh console, clean install from the release zip: works.
- [ ] Uninstall leaves no stray folder that breaks a reinstall.
- [ ] Crash produces a log on USB, not a silent exit.

## Result block (copy into the phase report)

```
firmware:            exploit:
build sha256:        sdk:            toolchain:
P2 boot:             P3 tls:         P3 dlopen:
P2 resources (cpu/mem/limits/mounts/workers/buffered):
P3 write app/homebrew/usb:
P4 small/1GB/resume/space/cancel/peak-mem:
P5 external plugin:  P5 manifest:     P5 isolation:
P6 session:
failures (exact error text):
not tested:
```
