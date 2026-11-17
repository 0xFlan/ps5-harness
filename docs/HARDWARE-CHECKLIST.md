# Hardware checklist

Run once per attempt. Copy the block at the bottom into a report and fill it in.
A phase is not complete because a build succeeded elsewhere; it is complete when
the relevant rows below are ticked on the console.

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

- [ ] Toolchain path used (container / VM / remote) recorded with image tag.
- [ ] Payload SDK version recorded.
- [ ] `libcurl` provenance recorded (PacBrew package name and pin).
- [ ] Artifact contains `eboot.bin`, `sce_sys/icon0.png`, `sce_sys/pic0.dds`,
      `sce_sys/pic1.dds`, `sce_sys/param.json`.
- [ ] Title ID in `param.json` is unique on this console.
- [ ] Artifact staged to the console by whatever method the loader expects.

## P2 — Boot

- [ ] Title appears in the shell with the expected name and icon.
- [ ] Launches without a system error dialog.
- [ ] Writes a log line containing build id and free space.
- [ ] Exits cleanly (no hang on quit).

Log path:

## P3 — Capabilities (each independent; partial results are valid)

### TLS
- [ ] Clock verified as correct immediately before this test.
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
P3 write app/homebrew/usb:
P4 small/1GB/resume/space/cancel/peak-mem:
P5 external plugin:  P5 manifest:     P5 isolation:
P6 session:
failures (exact error text):
not tested:
```
