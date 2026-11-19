# PS5 Harness — self-hosted release

This bundle is designed to run entirely on hardware you control. **There is no
project-operated server, relay, account service or update service.** The only
listeners are the ones you start on your console. In the built-in local mode,
model requests do not leave the console at all. In cloud mode, requests go
directly from the console to the provider you configure.

The harness is a userland payload. It needs an already-jailbroken console and a
loader such as `elfldr`; this bundle does not contain an exploit or jailbreak.
Use the loader and firmware combination you have already validated.

## Bundle layout

```text
START-HERE.md                 this file
README.md                     full project documentation
LICENSE                       MIT licence for the harness
config.example.json           host-side configuration shape; no credentials
config.console.json           ready-to-use cloud config; no credentials
config.console-local.json     ready-to-use on-console model config; no key
THIRD-PARTY-NOTICES.md        notices for statically linked libraries
licenses/                     full third-party licence texts
PHAR00001/                    installable title folder
  eboot.bin
  harness.elf
  sce_sys/
  models/                     optional GGUF when built with `--model`
examples/                     agents, endpoints, missions and skills to stage
tools/ps5h-discover.py        local-LAN discovery helper
docs/                         security, recovery, local-model and hardware notes
BUILD-INFO.txt                version, commit, package identity, model hash
SHA256SUMS                    checksums for every file above
```

Verify the download before copying anything to the console:

```bash
shasum -a 256 -c SHA256SUMS     # macOS
sha256sum -c SHA256SUMS         # Linux
```

## Third-party licences

The archive contains the complete third-party notices in
`THIRD-PARTY-NOTICES.md` and the corresponding full licence texts in
`licenses/`. Read them before staging; both the notices and every licence file
are covered by `SHA256SUMS`. The payload statically includes curl, OpenSSL,
zlib, Zstandard (and its embedded xxHash), libpsl/Public Suffix List data, and
the LLVM C++ runtime (`libc++`, `libc++abi` and `libunwind`).

## What you need

- A PS5 that is already on a jailbroken firmware with a working loader.
- The console and the browser on the same local network.
- **For on-console local mode:** the `Qwen3.5-0.8B-Q4_K_M.gguf` model file
  (532,517,120 bytes). No cloud key and no PS5LM install are needed. Download
  it from
  <https://huggingface.co/unsloth/Qwen3.5-0.8B-GGUF/resolve/main/Qwen3.5-0.8B-Q4_K_M.gguf?download=true>.
  Read the model card for its licence.
- **For cloud mode:** a model provider URL and an API key that you control. The
  key is read from the environment named by `api_key_env` (`PS5H_API_KEY` by
  default), or from a one-line file named by `model.api_key_file` /
  `--api-key-file`; the key itself is never stored in the config file or this
  bundle.
- No account with the project and no server started by the project.

Do **not** expose the harness listener directly to the internet. It is a
LAN service with a bearer token, not a hardened public web server.

## Stage the bundle

Use a file manager or your loader's file-transfer method. The local setup needs
this layout:

```text
PHAR00001/harness.elf                          ->  /data/homebrew/PHAR00001/harness.elf
PHAR00001/sce_sys/                             ->  /data/homebrew/PHAR00001/sce_sys/
PHAR00001/models/Qwen3.5-0.8B-Q4_K_M.gguf      ->  /data/homebrew/PHAR00001/models/Qwen3.5-0.8B-Q4_K_M.gguf
config.console-local.json                      ->  /data/ps5h-local.json
```

If you built the bundle with `make local-kit`, the model is already inside
`PHAR00001/models/`. Otherwise copy the downloaded GGUF to that folder.

`config.console-local.json` is already adapted for the console: local mode is
on, the model path points at `/data/homebrew/PHAR00001/models/`, and no API key
is stored in it. Edit the path only if your files live somewhere else.

For cloud mode, stage `PHAR00001/harness.elf` and
`config.console.json` -> `/data/ps5h-config.json` instead.

This bundle does not contain an uploader or a project-operated service. If your
loader puts the payload somewhere other than `/data`, change the paths in the
launch command below.

## Start the console workspace

With `elfldr` listening on port 9021, launch the local model. Replace the
console address and the token. The token must be at least 16 characters.

```bash
printf 'file:/data/homebrew/PHAR00001/harness.elf?args=--config%%20/data/ps5h-local.json%%20--serve%%208787%%20--serve-token%%20<your-long-token>%%20--session%%20/data/homebrew/ps5h-chat.json\n' \
  | nc -w 60 <console-ip> 9021
```

The same launch shape is in the full README. `--serve` is opt-in; without it the
harness runs one command and exits. The page is served by the harness process
on the console, not by this repository or its authors.

Wait for these lines in the console log:

```text
[local] model bytes 532517120
[harness] local model ready: /data/homebrew/PHAR00001/models/Qwen3.5-0.8B-Q4_K_M.gguf on port 18081
[harness] browser UI listening on 0.0.0.0:8787
```

For a cloud provider, use `config.console.json` and the same launch command,
with `/data/ps5h-config.json` in place of `/data/ps5h-local.json`.

Open:

```text
http://<console-ip>:8787/?token=<your-long-token>
```

`8787` is an example free port. If it is already occupied (for example by a
previous test listener), choose another free port and use that same number in
`--serve`, the browser URL and the discovery helper's `--ports` argument; the
discovery document and helper report the actual listener port.

If the console address or port changes, use the discovery helper from the
bundle root. Put the same token in `PS5H_TOKEN` so the printed URL is usable:

```bash
PS5H_TOKEN='<your-long-token>' python3 tools/ps5h-discover.py --ports 8787
PS5H_TOKEN='<your-long-token>' python3 tools/ps5h-discover.py --subnet 192.168.1.0/24 --ports 8787
```

If the discovery port is not 8799, add `--discovery-port <port>` to the helper
and pass the same value to the harness at launch.

For a repeatable setup, keep the same `--serve-token` and `--session` path
across relaunches. A token generated because `--serve-token` was omitted changes
on every launch.

## On-console model (no key)

The local model is built into `harness.elf`. It starts a llama.cpp server on
`127.0.0.1:18081/v1` inside the harness process. There is no second app to
install and no PS5LM dependency. The local branch sends no `Authorization`
header and accepts only that exact loopback endpoint.

The general URL policy is unchanged. Ordinary model endpoints must use HTTPS,
and private or loopback addresses are still rejected. Local mode is a narrow,
separate path; it is not a general SSRF exemption or a compile-time test hook.

Prove it with a real tool call. In the browser send:

```text
Call the fs_list tool with arguments {"path":"/data/homebrew"}. Do not write the command as text. Then tell me how many entries were returned.
```

A working run logs `tool fs_list {"path":"/data/homebrew"}` and answers from the
real entry count. On one test console this took 32.0 seconds for the first cold
answer and 14.5 seconds for the tool turn. Do not expect laptop speed.

Troubleshooting:

- Confirm the model file size is exactly 532,517,120 bytes.
- Confirm `local_model_path` in `/data/ps5h-local.json` matches the file.
- Keep the harness and model under `/data/homebrew`.
- Use 3 threads and a 4096-token context on a base PS5.
- Do not run PS5LM on port 18081. It is not needed.

## Cloud model key

In cloud mode the harness sends model requests directly to the provider at
`model.base_url`. Set the key in `PS5H_API_KEY` in the environment of the
process that launches the payload; do not paste it into a config file, a shell
transcript or a support message. If your loader cannot pass environment
variables, stage a one-line key file at `/data/ps5h-secrets/api-key` and add
`"api_key_file": "/data/ps5h-secrets/api-key"` to the model block, or pass
`--api-key-file /data/ps5h-secrets/api-key`. The harness reads it at startup and
redacts it from logs; the `ps5h-secrets` directory is denied to the agent's
file tools by the existing secret-name policy. The page and tools still work
without a key; cloud model chat is the part that needs one.

## Optional HTTPS

HTTP is the simplest local path. HTTPS is required for browser microphone APIs.
Generate a certificate whose SAN contains the console's current LAN address and
pass `--serve-tls <port> --tls-cert <file> --tls-key <file>`. Trust the
certificate only on the client devices you control. The private key stays on
the console; never put it in this release bundle or a support transcript.

## Recovery and limits

The harness is not a background service. It does not execute while the console
is asleep and it does not keep a terminated process alive. Suspended listeners
rebind after wake; a terminated payload or lost jailbreak needs a manual
relaunch. See `docs/RECOVERY.md` for the full sequence.

For a listener you started only for testing, `POST /api/shutdown` with the
bearer token stops the harness cleanly; without the token it returns 401. This
stops the payload, not the console.

No project server is contacted during startup, discovery, or browser use. In
local mode the model traffic stays on the console's loopback interface. In
cloud mode the only model traffic goes to the provider you configure; tool
network access still goes through the configured URL policy.
