# Hardware checklist

Run once per attempt. Copy the block at the bottom into a report and fill it in.
A phase is not complete because a build succeeded elsewhere; it is complete when
the relevant rows below are ticked on the console.

The 2026-11-18 attempt (build `0.1.0-fd5d0ae` for P2–P4, `0.1.0-4d7499f`
for local agents, `0.1.0-c0f76b5` for delegation, `0.1.0-653d046` for skills
and `fs_edit`, `0.1.0-e0b6af4` for recon/plan/swarm/missions, and
`0.1.0-e6b6205` for the parser-hardening rebuild and `0.1.0-01c6cbc` for the
home-screen tile, elfldr payload path) is recorded in `docs/CONSOLE-REPORT.md`: P2
boot/resources, P3 TLS/`dlopen`/writes, the P4 resume/1 GB/free-space checks,
the local-agent list/info checks, the delegation tool/failure checks and the
skill/edit checks are filled there; the title-install and USB rows remain open.
The harness is a userland payload. PS5 rest mode usually freezes it rather than
killing it, but closes its listening sockets. The in-process listener loop
detects `SIGCONT`, a wall-clock jump of at least five seconds, or an IPv4
network change; it keeps still-valid HTTP/HTTPS/UDP listeners serving during
the outage and retries binds at 2s, 4s, 8s, 16s and a 30s cap. A terminated
process or lost jailbreak is not covered. The rebind path needs a hardware run
before automatic resume is treated as verified.

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

The release bundle's `PHAR00001/` directory contains the real agent
(`eboot.bin` built from `harness.elf`) plus its SFO and icon. A source checkout
has the dependency pins and build commands under `platform/ps5/`.

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

## Local recon, plan, swarm and mission

Run these on the same payload build. They are read-only except where noted.

- [ ] Log/build line names the expected commit id and `title=PHAR00001`.
- [ ] `--tools` includes `env_info`, `fs_tree`, `file_info`, `fs_strings`,
      `delegate` and `swarm`.
- [ ] `--plan --tools` has no `fs_write`, `fs_edit`, `http_get`, `download`,
      endpoint or plugin tool; record the count (expected 9 with no endpoint
      directory staged).
- [ ] `--plan --call fs_write` returns `unknown tool: fs_write`, and the target
      path does not exist afterwards.
- [ ] `env_info` reports `platform=ps5`, the expected roots, `skills_dir`, CPU
      count, visible memory and a bounded system-mount list. Record
      `mounts_total`, `mounts_shown` and `sandbox_mounts_omitted`; the last two
      should show that other titles' sandbox mounts were filtered.
- [ ] `fs_tree` on `/data` marks `ps5-harness.json` and the harness logs as
      `denied`, does not follow a symlink, and caps a deep tree.
- [ ] `file_info /data/harness.elf` returns format, architecture, hash and
      `ET_DYN`; compare the hash with the uploaded artifact.
- [ ] `fs_strings /data/harness.elf` returns bounded strings and does not
      execute it.
- [ ] `skill` lists the six built-ins plus any staged packs; loading
      `reverse-engineer-anything` returns its instructions with no resources.
- [ ] A `swarm` call with nine lanes is refused before any child starts.
- [ ] A one-step mission with a missing profile runs and stops at that step with
      `ok:false`; record the step id and error.
- [ ] With a provider key staged, run a plan prompt, a two-lane swarm and a
      two-step mission. Record the provider/model, step counts, lane ordering
      and whether prior-step context reached the next task. Without a key, mark
      this row **not run** rather than inferring it from `--call`.

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
- [ ] `--list-agents` and `--agent-info` resolve staged profiles without a
      provider key.
- [ ] `--tools` lists `delegate`; `--call delegate` with a missing profile
      returns `ok:false` and logs `delegate !`.
- [ ] A real parent→child delegated model turn runs on hardware (requires a
      staged provider key).
- [ ] `--tools` lists `skill` and `fs_edit`; a staged `<name>/SKILL.md` pack is
      listed and loaded, and a bundled resource reads back.
- [ ] A skill resource traversal (`../../etc/hosts`) and an `fs_edit` outside
      the write roots are both refused with no write.
- [ ] `fs_edit` changes one unique match; a two-match edit is refused until
      `replace_all` is set, and the file is unchanged by the refusal.
- [ ] A plugin that crashes or hangs does not take the harness with it — record
      which isolation strategy was in place.

## P6 — Session

- [ ] `--serve <port>` serves the workspace; `/` returns the page and
      `/api/state` returns 401 without the token. (The 2026-11-18
      `0.1.0-c2c533f` smoke passed both, and authenticated state listed all 20
      tools including `fs_search`, `hexdump` and `sysctl`.)
- [ ] `GET /.well-known/ps5h` returns 200 without a token and exposes only
      service/build/title/ports/addresses; it contains no token or session.
- [ ] A UDP `PS5H_DISCOVER_V1` query to 8799 returns the same document, and
      `tools/ps5h-discover.py` finds that address/port.
- [ ] Stop and relaunch the payload with the same fixed token, session file and
      a different `--serve` port; discovery reports the new port, the browser
      reconnect banner accepts the new address, and the transcript resumes.
- [ ] After an actual rest-mode wake, record whether the jailbreak and the
      harness process survived. Do not relaunch first: wait a few seconds, then
      retry the original URL and check `/api/health` for an increased
      `listener_generation`. If it answers, confirm the saved transcript and
      both HTTP/HTTPS plus UDP discovery. If it does not, re-jailbreak/relaunch
      and complete the recovery runbook. Nothing is expected to answer while
      the console is asleep.
- [ ] A provider key is staged and a full turn runs through the browser UI;
      then restart the payload and confirm the transcript resumes from the
      session file.
- [ ] Open the page on a phone over the LAN and confirm the tool/activity panels
      remain usable at that width.
- [ ] Ten minutes of use with no terminal: browse, download, cancel, locate.
- [ ] Queue work on an always-on host while the console is asleep, then run it
      after wake. If the suspended harness survived, the queued work can run
      after the listener rebinds without relaunching; if the process or
      jailbreak was lost, relaunch `harness.elf` first. No row here can pass
      while the console is asleep.
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
