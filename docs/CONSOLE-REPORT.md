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
| Date | 2026-11-18, ≈09:18–10:00 local |
| Host | macOS 26.6.2 (arm64), LLVM 23.1.4 |
| SDK | ps5-payload-sdk v0.45 (zip sha256 `5fac6409…`) |
| Libraries | PacBrew v0.41.1 (bundle sha256 `e811bc2e…`), libcurl 8.18.0, OpenSSL 3.5.2 |
| Agent build | `0.1.0-fd5d0ae` |

Artifacts measured on disk:

| File | Bytes | sha256 |
|---|---|---|
| `eboot.bin` | 7,189,456 | `c195ab1c643eef5e393a5e1db8006be043f169aed383bf5378f824d97e41e94c` |
| `harness.elf` | 9,087,960 | `be7121ce86a805a83dc2595054c6bb980eaec47b4a26c123226472fc6f67fb35` |
| `sce_sys/param.sfo` | 376 | `6bf5d47549c9314c6cf5b5355cae17f1603c210ff6aaafa52978221c6e19803f` |
| `sce_sys/icon0.png` | 4,223 | `38c53aee2109dc4b3200b98dcacf8a294388b84436c97298933abb0947ebbef8` |

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
   renamed the file.

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

### 1 GB transfer

- **Source** — `https://proof.ovh.net/files/1Gb.dat`, 1 GiB of zero bytes with
  Range support (Cloudflare's speed endpoint refuses Range and its 1 GiB size,
  so it was not usable for this test).
- **Run** — a 64 MiB prefix was seeded, then the agent resumed with a Range
  request and transferred the remaining 1008 MiB continuously at ≈0.75 MB/s.
  The file reached exactly **1,073,741,824 bytes**.
- **Finalize** — the build in use for this transfer (`0.1.0-2e6d21c`) then hit
  the 64 KiB stack frame in `sha256_file_hex` and died before printing or
  renaming. The complete `.part` was left behind.
- **Integrity** — the completed file was fetched off the console over the LAN
  and hashed on the Mac: sha256
  `49bc20df15e412a64472421e13fe86ff1c5165e18b2afccf160d4dc19fe68a14`, the digest
  of 1 GiB of zero bytes.
- **After the fix** — a 10 MiB file seeded with a 1 MiB `.part` returned `206`,
  matched its expected sha256 and renamed into place. The digest path now
  streams in 8 KiB chunks and does not allocate by file size.
- **Peak memory** — not measured. The transfer streams through curl's write
  callback to a `FILE*`; no buffer grows with the file.
- The completed 1 GiB file remains at `/data/dl-1gb.bin.part`; a rename through
  the file-manager API returned a `system_error`, so the path is recorded here
  rather than presented as a clean `/data/dl-1gb.bin`.

## P5 — extension contract

The `hello` plugin was built outside the package with only
`include/ps5h/plugin_api.h` and the SDK, then loaded by the console binary. The
ABI check (`host->abi_version >= 2`), tool registration and handler execution
all worked. Plugins are in-process; a plugin that crashes takes the harness with
it. That isolation choice was not tested with a crashing plugin.

## Still unverified

- Title install and launch of `eboot.bin` (fake SELF, SFO keys, shell entry).
- Console firmware and exploit versions; the loader and web file manager
  versions.
- USB writes and an app-sandbox write.
- Peak RSS during a large transfer.
- A real model turn on hardware; no provider key was available to the loader.
- The exact CA bundle path and resolver used.
