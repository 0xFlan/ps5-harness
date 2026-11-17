# PS5 cross-build (P2)

Status: **not executed.** This machine cannot produce the artifact — the payload
SDK targets Linux. Everything below is the recipe to run on a Linux/WSL host, in
order, with the checks that decide whether the step actually worked.

Deliberately unstated: exact compiler-wrapper and toolchain-file names. Take them
from the SDK's own README at the version you pin, rather than from this file, and
record what you used in `docs/HARDWARE-CHECKLIST.md`.

## 0. Decide and record the environment

Pick one and write it down (PLAN open question P2):

- [ ] Docker Linux container on the Mac
- [ ] Linux VM (UTM/Parallels/QEMU)
- [ ] Remote/CI Linux builder

Record image name and tag. Without it a failure cannot be reproduced later.

## 1. Host toolchain

- [ ] Ubuntu/Debian base (24.04 or newer) — the SDK does not target macOS.
- [ ] `clang`, `lld`, `make`, `cmake`, `git`, `curl`, `python3`.
- [ ] `make doctor`-style check if the SDK provides one; otherwise confirm the
      compiler wrapper reports a PS5/FreeBSD target triple, not `darwin` or
      `linux-gnu`. **Check:** the triple is the proof, not the command exiting 0.

## 2. Payload SDK

- [ ] Clone `ps5-payload-dev/sdk`, pin a tag or commit, record it.
- [ ] Export the environment variables its README requires (SDK path, toolchain
      file) in the shell you build from.
- [ ] **Check:** compile a trivial `main()` for the target and inspect the output
      with `file` — it must be a FreeBSD/PS5 ELF, not a Mach-O or Linux ELF.

## 3. libcurl from PacBrew

The harness needs HTTP and TLS on console; do not hand-roll either.

- [ ] Fetch PacBrew's pinned prebuilt ports image (same pin as the SDK).
- [ ] Add `curl` (and its TLS dependency) to the package set.
- [ ] **Check:** the link line references the port's `libcurl`, not a host
      library; `ldd`-equivalent on the produced ELF shows no host paths.
- [ ] Ship a CA bundle in the app folder. A console system store is not assumed.

## 4. Build the app

- [ ] `make` with the SDK's compiler and the port include/library paths.
- [ ] **Check:** no host absolute paths in the binary (`strings` sweep), and the
      binary does not reference `darwin`.
- [ ] Confirm the sources compile with exceptions and RTTI **off** if the SDK
      defaults to that; if so, `std::future`/`std::async` (used by the tool
      deadline) and `std::function` must still work — the deadline path is the
      first thing to verify, since it is the only threading in the core.

## 5. Package

- [ ] Layout:

```
PPSA99177/
  eboot.bin
  sce_sys/param.json
  sce_sys/icon0.png      512x512
  sce_sys/pic0.dds       3840x2160 BC7
  sce_sys/pic1.dds       3840x2160 BC7
```

- [ ] Title ID changed from the placeholder `PPSA99177` and unique on the
      console; `contentId` contains it and ends with 16 uppercase alphanumerics.
- [ ] A PNG renamed to `.dds` is not sufficient — the images must be real BC7.

## 6. First boot (P2 completion)

- [ ] Title appears in the shell with the right name and icon.
- [ ] Launches with no system error.
- [ ] Writes one log line containing the build id and free space.
- [ ] Exits cleanly.

Then, and only then, P3: TLS, `dlopen`, elevated write — each recorded separately
in `docs/HARDWARE-CHECKLIST.md`.

## Known risks at this stage

| Risk | Symptom | First check |
|---|---|---|
| No exceptions/RTTI in the SDK build | Link errors around `std::function`/`std::future` | Compile the deadline path first |
| CA bundle missing | TLS fails with a verification error, not a timeout | Bundle present and path logged |
| Console clock wrong | Cert "not yet valid" / "expired" | Time before certificates |
| `dlopen` unavailable | Plugin load fails, manifest tools fine | `dlerror()` text |
| Sandbox write denial | `fopen` returns null, path is correct | Own folder vs `/data/homebrew` vs USB |
| Elevation conflicts with the loader | Write worked before a payload update, not after | Retest after any payload change |
