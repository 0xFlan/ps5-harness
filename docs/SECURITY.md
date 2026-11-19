# Security model

Scope: the harness running on a jailbroken PS5, the manifests and plugins other
people write for it, and the model provider it talks to.

## Assets, in priority order

1. **API keys and tokens** — the model key is the most valuable thing in the
   process; a stolen key is someone else's bill and, if it is account-scoped,
   someone else's account.
2. **The user's files** — anything outside the sandbox the harness can reach,
   including the console's own data and mounted USB drives.
3. **The user's network** — the console sits inside a LAN that often trusts its
   own devices. Anything that fetches arbitrary URLs from the console is
   potentially a probe of that LAN.
4. **Availability** — a wedged or crashed harness with no debugger is a support
   problem, not a security one, but it hides attacks.

## Adversaries

| Adversary | Capability | Primary defence |
|---|---|---|
| Malicious manifest author | Supplies JSON the harness loads | Data-only tier; https + host policy; no filesystem access |
| Malicious plugin author | Arbitrary code in-process | Trust tier: same permissions as a program the user runs |
| Prompt-injected content | Text the model fetches (pages, descriptions, API output) | Tool output is data; no instruction-following from fetched text |
| Compromised or curious model provider | Sees every message and tool result | Redaction; never place secrets in context |
| LAN attacker | Can reach the console's ports | No listening service by default; auth if `--serve` is added |
| LAN scanner | Can query the public discovery document and UDP beacon | Discovery returns only service/build/title/ports/addresses; no token, session, tools or paths; UDP can be disabled |
| The model itself, misused | Chooses tools and arguments | Allowlists, deadlines, canonicalised paths, least privilege |

## Trust boundary

```
  model provider  ──(messages, tool results)──►  harness process
                                                      │
                    ┌─────────────────────────────────┼──────────────────┐
                    │                                 │                  │
             manifest JSON                       plugin .so         filesystem
             (data, parsed)                   (code, in-process)   (canonicalised)
```

Everything left of the harness is untrusted. Everything the harness opens is a
capability it chose to grant; nothing is inherited by accident.

## Invariants

These are not guidelines. Any change that weakens one is a regression, and the
test that proves it must ship with the change.

1. **Paths are canonicalised before any filesystem operation.** `realpath` the
   root at startup, canonicalise the target, require `target == root` or
   `target.startswith(root + "/")`, resolve symlinks, reject `..` and embedded
   NUL. Prefix comparison alone is a bug (see A1). The same rule covers
   `fs_write`, `fs_edit`, skill resources and profile prompt files.
2. **Secrets are never in the context window.** Credentials come from
   environment variables, are read at call time, and are never written to
   config, logs, tool results, or docs. Every tool result passes a redaction
   pass against known secret values before it enters the message array.
3. **Manifests are https-only, host-policed, and cannot reach the LAN.**
   Loopback, link-local, and private ranges are denied unless explicitly
   allowlisted; redirects are disabled for manifests; DNS is re-checked after
   resolution.
4. **Downloads stream to disk.** No code path buffers an unbounded response in
   memory; buffered responses have a hard byte cap.
5. **Every tool call has a deadline.** Downloads use an inactivity timeout
   instead.
6. **Fetched text is data.** No tool output, page body, or manifest description
   is treated as an instruction. The system prompt says this explicitly and the
   tool-result wrapper marks provenance.
7. **Plugins are code.** Loading a plugin directory is equivalent to running
   that program. The harness never auto-loads plugins from a network source,
   never from a registry, and never without an explicit path or config entry.
8. **The model endpoint is policy-checked.** The provider URL is where the API
   key travels, so it goes through the same https-only, no-private-range check
   as everything else. A cleartext or LAN endpoint is refused before a socket
   opens. The only exception is a compile-time test hook that is absent from
   production builds and limited to loopback cleartext (see below).
9. **No capability is inferred from privilege.** If the console denies a write,
   the tool reports the denial and the exact path; it never retries with a
   different mechanism to get around the sandbox.
10. **Delegation never widens a capability.** A child profile only selects a
    prompt, model, temperature, token cap and step cap. It receives a copy of
    the parent's tools and therefore the same canonicalised roots, URL policy,
    secrets and endpoint/plugin surface. Profiles cannot set boundary keys
    (`allowed_roots`, `read_roots`, `url_policy`, `secret_env`, `endpoints_dir`,
    `plugins_dir`, `skills_dir`, `api_key_env`) or `delegation_max_depth`.
    Nesting depth, self-delegation and the shared parent step/token pool are
    enforced before a child starts; refusals return a clear error and are
    logged.
11. **Skills are instructions, not capabilities.** A skill is a directory under
    the configured skills root with a size-capped `SKILL.md` and optional
    bundled files. Names are restricted, every path is canonicalised and must
    stay inside that skill's directory, and secret-named paths are denied.
    Loading a skill registers no tool and changes no root, URL policy, secret
    or delegation limit. A profile cannot set `skills_dir`. Built-in packs are
    compiled-in first-party text, contain no executable content and are
    overridden by a disk pack with the same name; they are not an audit of any
    external project that shares a name.
12. **Plan mode is enforced by the registry.** `--plan` removes `fs_write`,
    `fs_edit`, `fs_mkdir`, `fs_move`, `fs_delete`, `http_get`, `download` and all
    endpoint/plugin tools before any model or delegated child can call them.
    The prompt overlay is guidance; the missing tool is the control. A
    read-only run still uses the same canonicalised roots, secret denial,
    deadlines and delegation depth. `fs_search`, `hexdump` and `sysctl` are
    bounded read probes and remain available.
13. **Recon is read-only and bounded.** `env_info`, `fs_tree`, `file_info` and
    `fs_strings` never write or execute a target. They use the read guard and
    secret-name rules, cap tree entries/strings/hash size, do not follow
    symlinks, and `env_info` omits other titles' sandbox mounts. `file_info`
    hashes at most 64 MiB; `fs_strings` reads at most 16 MiB and returns at most
    200 strings.
14. **Swarms and missions cannot widen delegation.** Every lane and mission
    step is a normal delegated child: the shared step/token pool, depth limit,
    self-delegation check and inherited registry all still apply. Width is
    hard-capped at 8, missions at 32 steps/8 lanes/8 KiB per task, and the
    mission runner stops at the first failed step. A mission file composes
    agent turns; it cannot execute shell commands or branch into an unbounded
    loop.
15. **The browser listener is explicit and authenticated.** No socket is
    opened unless `--serve` is passed. It binds the LAN interface, but every
    `/api/*` resource requires `Authorization: Bearer <token>`; the token is
    generated from `/dev/urandom` when not supplied, must be at least 16
    characters when supplied, is printed once to stderr (never to the log
    file), and is compared with a full-length constant-time check after the
    length check. The
    page is public only so it can draw the token prompt. There are no cookies,
    no CORS headers, no wildcard origins and no unauthenticated state
    mutation. The server serialises turns with a per-process lock, caps request
    bodies, closes every connection after one response, and serves no path
    from disk. The session file is checked against the write roots and the
    secret-name rules before use. Keep the listener off untrusted networks and
    stop it when finished. Two public read-only exceptions are deliberate: the
    page/tile icon needed to draw the token prompt, and `/.well-known/ps5h`
    plus its UDP query. Discovery exposes only service/build/title/ports and
    addresses, never the token, session, tools or filesystem paths. Set
    `--discovery-port 0` to disable UDP.
16. **Power tools do not bypass the write boundary.** `fs_mkdir`, `fs_move`
    and `fs_delete` use the same canonicalisation, root check and secret-name
    denial as `fs_write`. Delete is never recursive. `hexdump` and `fs_search`
    use the read guard and hard caps. `sysctl` is an allowlisted read probe: no
    shell, no process arguments, no environment or credential namespaces, and
    bounded output.

## Secret handling

- Config files contain **environment variable names**, never values. The
  current `config.example.json` already follows this; keep it that way.
- The key is read per request, kept in a local `std::string`, and not stored in
  any member of the agent that could be serialised.
- Logs at any level never print a header value, a query parameter named `key`,
  `token`, `api_key`, or `sig`, or an `Authorization` header. Redact before
  formatting.
- If a key is compromised: rotate at the provider, then purge the harness log
  and any session transcript. Treat the console as untrusted until rotated.
- Do not add a "store key in settings" feature without an OS-keyring equivalent.
  On this platform there isn't one, so prefer: key from environment, or a
  short-lived token minted by a proxy the user runs elsewhere.

## Test-only network hook

`UrlPolicy::test_allow_loopback_cleartext` and the CLI flag
`--test-allow-loopback-cleartext` are compiled only when `PS5H_TEST_HOOKS` is
defined, which only the test targets do. They allow cleartext HTTP to a loopback
host and nothing else — no LAN, no private ranges, no other scheme. No config
file can enable them, `tests/policy_surface_test.cpp` fails to compile if the
field appears in a production build, and `make test` asserts the shipped binary
rejects both the cleartext endpoint and the flag.

## Plugin and manifest trust tiers

| Tier | What the user is trusting | Default |
|---|---|---|
| Built-in tools | This repo | On |
| Built-in skill pack | This repo's text only; no code or network; read-only | On |
| Skill pack | Local instructions the model may follow; read-only and path-confined | On for the configured directory |
| Manifest | Network access to the declared host; the JSON was reviewed | Off unless a directory is passed |
| Plugin | Arbitrary code, network, filesystem within sandbox | Off; explicit path only |
| Registry index | Nothing beyond transport integrity — entries are not vouched for | Off |

The registry must never claim that listing equals safety. Say plainly, in the
docs and in the app, that the catalog is a convenience index and each entry is
the author's own.

## Privacy

Tool results and conversation history are sent to the model provider. That
includes file contents the user asked to read, API responses from manifests and
any skill instructions or bundled resources loaded with `skill`. `env_info`
reports mount paths and resource counts, but filters other titles' sandbox
mounts and never reads configuration or credential files; `fs_tree` marks
denied names without reading their contents. The browser workspace sends the
same transcript to the provider and stores it in `<first write root>/ps5h-session.json`
(or `--session`) unless the user resets the session; that file stays local,
under the write roots, and is never served as a web asset. `.env` and the other
secret-name patterns are denied by the same path check. Say so in the UI the
first time a file is read, and keep session transcripts local with a clear
delete path.

## Submission checklist (before the catalog PR)

- [ ] No piracy surface: no ROM, torrent, or Vimm's Lair adapter, and no
      registry entry that is one.
- [ ] Every S1 and S2 item in `docs/AUDIT.md` closed with a proving test.
- [ ] Threat-model pass repeated against the final code, not the plan.
- [ ] `SHA256SUMS` published; release zip shape `<TITLE_ID>/` with `eboot.bin`
      and `sce_sys/`; `contentVersion` bumped.
- [ ] Title ID unique; `contentId` contains it and ends with 16 uppercase
      alphanumerics.
- [ ] License allows redistribution; source in the repo; `author` credited.
- [ ] Support matrix names exact firmware and exploit toolchain.
- [ ] Disclosure path documented: where to report a vulnerability, and the
      commitment not to publish before a fix.

## Incident response

1. Reproduce on the host build; if it needs hardware, record firmware first.
2. Rotate any credential that could have been exposed — before diagnosing.
3. Patch, add the proving test, then re-run the full audit.
4. If a manifest or plugin is implicated: remove it from the registry, publish
   what happened, and state the version window affected.
