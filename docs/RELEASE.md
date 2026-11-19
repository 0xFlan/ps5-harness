# PS5 Harness — self-hosted release

This bundle is designed to run entirely on hardware you control. **There is no
project-operated server, relay, account service or update service.** The only
listeners are the ones you start on your console, and model requests go directly
from the console to the provider you configure.

The harness is a userland payload. It needs an already-jailbroken console and a
loader such as `elfldr`; this bundle does not contain an exploit or jailbreak.
Use the loader and firmware combination you have already validated.

## Bundle layout

```text
START-HERE.md                 this file
README.md                     full project documentation
LICENSE                       MIT licence for the harness
config.example.json           host-side configuration shape; no credentials
config.console.json           ready-to-use console config; no credentials
THIRD-PARTY-NOTICES.md        notices for statically linked libraries
licenses/                     full third-party licence texts
PHAR00001/                    installable title folder
  eboot.bin
  harness.elf
  sce_sys/
examples/                     agents, endpoints, missions and skills to stage
tools/ps5h-discover.py        local-LAN discovery helper
docs/                         security, recovery and hardware notes
BUILD-INFO.txt                version, commit and package identity
SHA256SUMS                    checksums for every file above
```

Verify the download before copying anything to the console:

```bash
shasum -a 256 -c SHA256SUMS     # macOS
sha256sum -c SHA256SUMS         # Linux
```

## What you need

- A PS5 that is already on a jailbroken firmware with a working loader.
- The console and the browser on the same local network.
- A model provider URL and an API key that you control. The key is read from
  the environment named by `api_key_env` (`PS5H_API_KEY` by default); it is
  never stored in the config file or this bundle.
- No account with the project and no server started by the project.

Do **not** expose the harness listener directly to the internet. It is a
LAN service with a bearer token, not a hardened public web server.

## Stage the bundle

Use a file manager or your loader's file-transfer method to put these two files
on the console:

```text
PHAR00001/harness.elf   ->  /data/harness.elf
config.console.json     ->  /data/ps5h-config.json
```

`config.console.json` is already adapted for the console: endpoint and plugin
directories are empty, skills are read from `/data/homebrew/skills`, and no API
key is stored in it. Edit the paths only if your files live somewhere else.

This bundle does not contain an uploader or a project-operated service. If your
loader puts the payload somewhere other than `/data`, change the paths in the
launch command below.

## Start the console workspace

With `elfldr` listening on port 9021, launch the staged payload with a fixed
token and the workspace enabled:

```bash
printf 'file:/data/harness.elf?args=--config%%20/data/ps5h-config.json%%20--serve%%208787%%20--serve-token%%20<your-long-token>\n' \
  | nc -w 60 <console-ip> 9021
```

The same launch shape is in the full README. `--serve` is opt-in; without it the
harness runs one command and exits. The page is served by the harness process
on the console, not by this repository or its authors.

Open:

```text
http://<console-ip>:8787/?token=<your-long-token>
```

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

## Third-party notices

The release statically links libcurl, OpenSSL, zlib, zstd and libpsl. The
required notices and full licence texts are in `THIRD-PARTY-NOTICES.md` and
`licenses/`; both are included in `SHA256SUMS`.

## Model key and local-model boundary

The harness sends model requests directly to the provider at
`model.base_url`. Set the key in `PS5H_API_KEY` in the environment of the
process that launches the payload; do not paste it into a config file, a shell
transcript or a support message. If your loader cannot pass environment
variables, the page and the local tools still work, but model chat needs a
launch path that can set the variable.

The production URL policy deliberately rejects cleartext and private/loopback
addresses, including a model server on the console's own LAN. That is an SSRF
and credential-protection boundary, not a hosted-service dependency. If you
need a local model, expose it through a separately reviewed TLS endpoint; do not
weaken the policy or enable the compile-time test hook in a shipped binary.

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

No project server is contacted during startup, discovery, or browser use. The
only outbound traffic is to the model provider and to endpoints you explicitly
configure.
