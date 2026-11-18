# PS5 cross-build (P2)

Status: **the build is verified on this Mac; boot is not.** `make -C platform/ps5`
compiles the P2 bring-up payload to an x86-64 FreeBSD ELF and packs it into a
fake-SELF `eboot.bin`. No console has run it, so P2 is not complete and P3 has
not started.

## What was actually run

On 2026-11-17, macOS 26.6.2 (arm64):

- Payload SDK release zip, sha256
  `5fac64093d0e3e69f2584ce7935f24f703f622d9681f1495c85b9f00d1291a2c` (v0.45),
  unpacked to `.deps/ps5-payload-sdk` (ignored by git).
- LLVM 23.1.4 from Homebrew, `LLVM_CONFIG=/opt/homebrew/opt/llvm/bin/llvm-config`.
  The SDK's own wrappers select it; the SDK's README pins 18 but its
  `prospero-llvm-config` accepts 15–23.
- `make -C platform/ps5` produced `bringup.elf` (ELF 64-bit LSB executable,
  x86-64, FreeBSD) and `PPSA99177/eboot.bin` (fSELF, magic `4f153d1d`).
- Build at commit `77c0bfe`: build id `0.1.0-77c0bfe`, ELF sha256
  `74a4ae4800cadf7ad3acd8af6ef20ad7a18e6dbab413ae8461e3163bf12a19c4`,
  `eboot.bin` 58,880 bytes sha256
  `e37d71a5f982b5f1323c548aee4b6a8cc1a66689913d28d74b99a891c634bac0`.
  The build id embeds the commit, so any commit or source change alters the
  artifact hash; quote the build id, not just the hash, in a console report.
- Host checks on the same source: a native build writes the log and, with a
  `SIGSEGV` forced after handler installation, leaves
  `CRASH build=… signal=11 (SIGSEGV) addr=…` and exits 139. Neither check runs
  the FreeBSD binary; both only prove the logic runs on a host.

Reproduce:

```bash
mkdir -p .deps && cd .deps
curl -fsSLO https://github.com/ps5-payload-dev/sdk/releases/latest/download/ps5-payload-sdk.zip
unzip -q ps5-payload-sdk.zip
cd ..
export PS5_PAYLOAD_SDK="$PWD/.deps/ps5-payload-sdk"
export LLVM_CONFIG=/opt/homebrew/opt/llvm/bin/llvm-config   # any llvm-config 15–23
make -C platform/ps5
```

`make_fself.py` accepts `ET_EXEC`, not `ET_DYN`; the SDK driver defaults to PIE
on this target, so `platform/ps5/Makefile` passes `-Wl,--no-pie`. Without it the
pack step fails with `ElfError: 'Unsupported type.'`.

What this does **not** prove: that the fake SELF is accepted by the loader on
your firmware, that the default `--paid` is right for your setup, that the
mounts exist, or that a title launches. Those are console checks below.

## On a console

Record the environment first (PLAN open question P2): host OS and LLVM version,
SDK zip sha256, and the artifact's sha256 next to the firmware version. Without
those a failure cannot be reproduced.

### Pre-flight

- [ ] Ubuntu/Debian or macOS host with `clang`/`lld` 15–23, `make`, `python3`.
- [ ] Console on a firmware with a working payload loader; recovery path known.
- [ ] USB stick formatted and empty (the payload writes a log there if it can).
- [ ] Console clock correct if TLS will be tested later.

### Build and stage

- [ ] `make -C platform/ps5` succeeds; `file bringup.elf` says FreeBSD, and
      `xxd -l 4 eboot.bin` shows `4f15 3d1d`.
- [ ] `sha256sum` of `eboot.bin` recorded.
- [ ] Stage `PPSA99177/` (`eboot.bin` + `sce_sys/param.json`) by whatever method
      your loader expects.
- [ ] Title ID changed from the placeholder `PPSA99177` in `param.json`, unique
      on the console; `contentId` contains it.

### First boot (P2 completion)

- [ ] Title appears in the shell with the right name.
- [ ] Launches with no system error.
- [ ] Log line contains the build id and the free space of at least one mount.
- [ ] Exits cleanly.

The payload logs through a pre-opened fd, so a crash still appends. It records
build id, pid, uid, cwd, argv[0], then free and total MiB for each candidate
mount plus the `errno` of each one that cannot be stat'ed. Log paths tried, in
order: `/data/ps5-harness-bringup.log`,
`/user/homebrew/ps5-harness-bringup.log`, `./ps5-harness-bringup.log` (the app's
folder), `/mnt/usb0/…`, `/mnt/usb1/…`. A fatal signal writes one
`CRASH build=… signal=N (NAME) addr=0x…` line and exits `128+N`, so a failed
boot names its fault instead of going silent.

Then P3: TLS, `dlopen`, elevated write — each recorded separately in
`docs/HARDWARE-CHECKLIST.md`.

## Known risks at this stage

| Risk | Symptom | First check |
|---|---|---|
| Fake SELF rejected by the loader | Title does not appear, or system error on launch | `--paid`/`--ptype` values, loader docs for your firmware |
| `param.json` layout differs | Install fails or no icon | Validate against a working homebrew title on the same firmware |
| Sandbox write denial | No log file, but the title ran | Own folder vs `/data/homebrew` vs USB |
| CA bundle missing (P3) | TLS fails with a verification error, not a timeout | Bundle present and path logged |
| Console clock wrong (P3) | Cert "not yet valid" / "expired" | Time before certificates |
| `dlopen` unavailable (P3) | Plugin load fails, manifest tools fine | `dlerror()` text |
| No exceptions/RTTI (full harness) | Link errors around `std::function` | Compile the deadline path for the target first |
