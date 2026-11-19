# Connection and recovery runbook

This runbook covers the support cases around a console that moves, sleeps or
loses its jailbreak:

- the browser loses the console and later reconnects;
- the console enters rest mode;
- the customer restarts, re-jailbreaks and stages the payloads again;
- the console gets a different LAN address or the harness is started on a
  different port.

The harness is a userland payload. It is not a background service, and it does
not execute while the console is asleep. Recent PS5 homebrew reports show that
rest mode usually **freezes** a payload rather than killing it, but the system
closes its listening sockets and drops the LAN interface. The serving loop now
detects the resume and rebinds HTTP, HTTPS and UDP discovery. This is entirely
in-process: no separate payload or external component is required. It makes
the suspend case recoverable without a relaunch; if the process or jailbreak is
actually gone, the manual path below still applies.

## What survives what

| Event | Harness process | Session file | Fixed bearer token | Home tile folder | LAN address |
|---|---|---|---|---|---|
| Wi-Fi/Ethernet drop, then returns | Keeps running | Keeps | Keeps | Keeps | Same or new DHCP lease |
| Console rest mode (process suspended) | Usually survives; listeners are rebound | Keeps | Keeps if `--serve-token` was fixed | Keeps | May change after wake |
| Console rest mode (process terminated) | **Gone** | Keeps | Keeps if `--serve-token` was fixed | Keeps | May change after wake |
| Restart / re-jailbreak | **Gone** | Keeps | Keeps if `--serve-token` was fixed | Keeps; startup re-registers it | May change |
| New `--serve` port | Old process unchanged | Keeps | Keeps | Re-registered with the new deeplink port | Same console |

The browser transcript is persisted by `--session`. A finished or interrupted
turn is not automatically replayed after a restart; the saved transcript is
restored, and the user can continue from it.

## 1. The browser loses the connection

The workspace polls `/api/state` with a short request timeout and retries
while the page stays open. On failure it shows a **Console connection lost**
banner and keeps the session token in that tab's session storage.

1. Wait for the automatic retry. A dropped Wi-Fi link or a short router restart
   normally reconnects without a reload.
2. Press **Retry now** for an immediate attempt.
3. If the console has a new address or port, enter it in the banner and press
   **Use address**. The browser navigates to the new origin and carries the
   current token in the URL; the page removes it from the address bar after
   loading.
4. If the response is `401`, the payload is running with a different token.
   Enter the current token in the login prompt. A token generated because
   `--serve-token` was omitted changes on every launch; a fixed token avoids
   this.

The browser cannot discover a new IP address by itself. Use the discovery
helper or the console's own address display, then paste the result into the
banner.

## 2. The console went into rest mode

**No work executes while the console is asleep, and this repository does not
install a wake source, scheduler or background service.** A rest-mode wake
needs a human at the console or a working Remote Play wake.

On current PS5 homebrew reports, the payload process is usually frozen with its
memory intact; the system closes its listening sockets and takes the network
down. The harness watches `SIGCONT`, a wall-clock jump of at least five seconds,
and the non-loopback IPv4 address set. While the network is unavailable it
keeps the existing listeners in the poll set, so any still-valid HTTP, HTTPS or
UDP discovery request is answered promptly. Once the network is ready it
closes and rebinds the same ports. Network checks are capped at one second, and
bind failures back off at 2s, 4s, 8s, 16s and a 30s cap; a second wake resets
that wait. `/api/health` reports a new `listener_generation` after a successful
rebind.

After wake:

1. Confirm the console is awake and on the expected network. Give it a few
   seconds after the network returns.
2. Try the existing page. Its normal retry should reconnect on the same origin.
   If DHCP changed the console's address, use the reconnect banner's address
   field or run discovery below.
3. If the API responds with the saved transcript, the process survived; do not
   relaunch it just because the page showed a temporary lost-connection banner.
4. If the listener does not return, the process may have been terminated or the
   jailbreak may have been lost. Re-run the exploit as required and start the
   payload with the same launch configuration, session file and fixed token.
5. Open the workspace and confirm the saved transcript is present. The home
   tile is re-registered during startup when `--home-tile-autostart` is set and
   no removal marker is present.

`elfldr` and the file manager are separate processes and may still be
unreachable after wake even when the harness survived. There is no wake-on-LAN
in the harness. Queue jobs on an always-on host and run them after the console
is awake and the payload is running again.

## 3. Restart or re-jailbreak

The order matters:

1. Boot the console and let it finish starting.
2. Re-run the exploit/jailbreak if the loader requires it.
3. Re-stage or relaunch the same `harness.elf` with the same config, token,
   session path, web-shell path and homebrew root.
4. Wait for the browser listener line and the discovery line in the launch
   output.
5. Open the saved workspace or run discovery.
6. Check the home-screen tile status in the workspace. Startup refreshes the
   tile; a missing ShadowMountPlus service is reported as Pending, not fatal.

Loader ports are separate from the harness. `elfldr` on 9021 and the web file
manager on 8888 are not started by this payload and are not covered by the
harness discovery protocol. If the loader or file manager was set up on a
different port, update the staging/launch command; that is a loader concern.

Use a fixed token for a repeatable customer setup:

```bash
# Example launch shape; use the paths and token from the customer's setup.
./harness.elf --config /data/ps5h-mission.json \
  --serve 8791 --serve-tls 8790 \
  --tls-cert /data/ps5h-cert.pem --tls-key /data/ps5h-key.pem \
  --serve-token "$PS5H_TOKEN" --discovery-port 8799 \
  --session /data/ps5h-mission-session-final.json \
  --web-shell /data/ps5h-web-shell.bin \
  --homebrew-root /data/homebrew --home-tile-autostart
```

On the console the launcher is usually the `file:` URI through `elfldr`; keep
the same argument set there. Do not put the token in a committed file or in a
support transcript. The token is the only browser credential.

## 4. The IP address or port changed

The listener binds `0.0.0.0`, so a new LAN address does not require a source
change. A different serving port is selected with `--serve <port>`; when
auto-install is enabled (the console default), the home tile is rewritten with
the actual port at startup.

The staged HTTPS certificate's SAN is tied to the old address (currently
`192.168.4.100`). After an address change, the discovery helper prints the HTTP
origin; use that, or regenerate the certificate with the new address in its
SAN before relying on HTTPS.

### Discovery document

Every running listener exposes a public, unauthenticated identity document:

```bash
curl http://<console-or-old-address>:<port>/.well-known/ps5h
```

It contains only:

```json
{
  "ok": true,
  "service": "ps5-harness",
  "protocol": 1,
  "build": "...",
  "title_id": "HARN00001",
  "http_port": 8791,
  "https_port": 8790,
  "started_at": 0,
  "addresses": ["192.168.4.100"],
  "urls": ["http://192.168.4.100:8791/"]
}
```

It never contains the bearer token, session text, tool output or filesystem
paths. Ports are the ports that actually opened, not the requested defaults.

### UDP discovery

The primary listener answers a UDP query on port 8799:

```bash
printf 'PS5H_DISCOVER_V1\n' | nc -u -w 1 255.255.255.255 8799
```

A broadcast may be blocked on some networks. Send to the subnet's directed
broadcast address instead, or use the TCP document above. `--discovery-port 0`
disables UDP discovery. Only the primary listener should own 8799; a second
same-artifact companion on another HTTP port should use
`--discovery-port 0`.

### Host helper

`tools/ps5h-discover.py` uses only the Python standard library:

```bash
# UDP broadcast on the local LAN
python3 tools/ps5h-discover.py

# Keep watching; print a new URL when the address or port changes
python3 tools/ps5h-discover.py --wait

# If broadcast is blocked, scan the LAN for the public document
python3 tools/ps5h-discover.py --subnet 192.168.4.0/24

# Print a ready-to-open URL without putting the token in shell history
PS5H_TOKEN='<token>' python3 tools/ps5h-discover.py
```

The helper prints the console address, HTTP port, build, title and source. It
does not send the token in discovery. If `PS5H_TOKEN` is set, it only appends
the token to the printed URL.

## 5. Diagnosis

| Symptom | Most likely cause | Check |
|---|---|---|
| Browser says connection lost, but the console is awake | Payload exited, serving port changed, or Wi-Fi changed | Run the discovery helper; check the listener line in the launch output |
| Connection refused | No listener on that address/port | Start or relaunch the payload; try another `--serve` port |
| `401 unauthorized` | Wrong or newly generated token | Use the token from the current payload launch, or fix `--serve-token` |
| Discovery document returns 200, API returns 401 | The service is reachable; only auth is wrong | Re-enter the token |
| UDP discovery returns nothing | Broadcast blocked, old build, wrong port, or different subnet | Use `--subnet` TCP fallback or the public document |
| Console does not answer at all | Console asleep/off, wrong network, or jailbreak/loader lost | Wake/re-jailbreak and relaunch the payload |
| Home tile says Pending after restart | ShadowMountPlus not running or scan path missing | Start ShadowMountPlus, then Check status |

Do not infer that a timeout means the jailbreak is gone. Establish whether the
console answers at all, whether the valid listener answers, and whether the API
answers with the current token, in that order.

## 6. What this does not solve

- It does not execute work while the console is asleep.
- It does not keep a terminated payload alive, auto-start it, or repair a lost
  jailbreak; it only rebinds listeners when the suspended process survives.
- It does not wake the console or re-run the exploit automatically.
- It does not start the payload from the home tile; the tile opens the
  configured web-app shell, while the harness listener still needs its launch.
- It does not discover across routed subnets, VLANs or a guest-network boundary.
- It does not migrate a browser tab to a new origin automatically; the banner's
  address field or the discovery helper supplies the new URL.
- It does not rotate or protect a token. Use a fixed token, keep it out of
  logs and support transcripts, and rotate it by relaunching with a new one.

## Verification status

The public document, UDP responder, changed-port home-tile deeplink and host
discovery helper are covered by host tests and a loopback smoke run in this
tree. `make reconnect-test` starts local HTTP and HTTPS harnesses, loads the
real page in headless Chrome, stops and restarts one harness, switches the
banner to a changed origin/port with the token carried, verifies the transcript
reloads, and verifies a stale token produces the rejected-token login state. It
writes screenshots and `report.json` under `build/reconnect-evidence/`.

`server_test` simulates the suspend/resume lifecycle by sending `SIGCONT`
while a test hook holds the network down. It asserts that HTTP and UDP requests
are still answered by the old listeners during the wait, then restores the
network, requires the listener generation to advance and the transcript to
remain, and repeats the wake to prove a second resume recovers. `make
reconnect-test` also sends `SIGCONT` to its HTTPS harness and waits for a new
listener generation before driving the page, which exercises the TLS rebind
path. This does not reproduce the system-level socket closure of a real rest
cycle, and it does not exercise the console loader, jailbreak, or the real
network interface. A hardware rest-mode run is still required before treating
automatic resume as customer-verified.
