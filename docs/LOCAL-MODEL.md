# On-console model

The harness can run its language model on the PS5 itself. The model runtime is
part of the harness process. There is no separate model app, no PS5LM install,
no phone, and no cloud API key.

This page explains how it works, how it is built, and what was measured on a
real console. For the user setup steps, read
[`Run the model on the console`](../README.md#run-the-model-on-the-console-no-key)
and [`START-HERE.md`](RELEASE.md).

## What runs where

```text
PS5
  harness process
    agent and tools
    llama.cpp server
      model: Qwen3.5-0.8B-Q4_K_M.gguf
      listener: http://127.0.0.1:18081/v1     loopback only
    browser listener: http://0.0.0.0:8787     LAN, token protected
```

The harness starts a llama.cpp server on a thread inside its own process. It
waits for `GET /health` to return HTTP 200 before it serves the browser or runs
a prompt. A TCP connect alone is not enough: the listener opens before the
weights finish loading.

The model is a 0.8B Qwen GGUF quantised to Q4_K_M. On this build it is
532,517,120 bytes. It is small enough to leave memory for the browser, tools and
the console's own services. A larger model is possible, but memory is the
limiting factor on a base PS5.

## Why a separate local mode

The normal URL policy is deliberately strict. It accepts HTTPS only and rejects
private and loopback addresses. That protects the agent from being used as an
SSRF proxy and keeps cloud credentials away from arbitrary hosts.

The local runtime must use loopback. Rather than weakening the general policy,
the harness has a separate local branch:

- `local_model` must be true.
- The endpoint must be `http://` on `127.0.0.1` or `localhost`.
- The port must be explicit and must match a valid local port.
- The path must start with `/v1`.
- User info, query strings and fragments are rejected.
- The request sends no `Authorization` header and looks up no API key.

`local_model_endpoint()` in `include/ps5h/policy.h` implements this check. The
tests cover accepted loopback forms and rejected public, cleartext, user-info,
query and fragment forms.

## Configuration

`config.console-local.json` is the ready-to-use console config:

```json
{
  "model": {
    "local": true,
    "base_url": "http://127.0.0.1:18081/v1",
    "model": "local",
    "local_model_path": "/data/homebrew/PHAR00001/models/Qwen3.5-0.8B-Q4_K_M.gguf",
    "local_model_port": 18081,
    "local_model_threads": 3,
    "local_model_context": 4096,
    "api_key_env": "",
    "api_key_file": "",
    "temperature": 0.2,
    "max_tokens": 512
  }
}
```

The model path may be changed. Keep the file under `/data/homebrew` so the
existing write guard can reach it. Do not put the GGUF in the harness package's
read-only `sce_sys` folder.

`local_model_threads` is capped at 8 by the runtime. Three threads is the tested
value on a base PS5. More threads compete with the system and do not necessarily
make a small model faster. `local_model_context` is 4096 tokens; a larger
context uses more KV-cache memory.

CLI equivalents:

```bash
./build/harness --local-model /path/to/model.gguf
./build/harness --local-model-path /path/to/model.gguf --local-threads 3 --local-context 4096
```

The browser's Model panel also has an **On-console (built-in)** preset. It does
not ask for a key.

## Build the runtime

The harness links llama.cpp statically. It does not need a llama.cpp executable
on the console.

### Host

The host build expects the host static archives under `.deps/llama-host`. The
top-level `Makefile` links:

```text
tools/server/libllama-server-impl.a
tools/server/libserver-context.a
common/libllama-common.a
common/libllama-common-base.a
tools/mtmd/libmtmd.a
src/libllama.a
ggml/src/libggml.a
ggml/src/libggml-cpu.a
ggml/src/libggml-base.a
vendor/hash/libvendor-hash.a
tools/ui/libllama-ui.a
vendor/cpp-httplib/libcpp-httplib.a
```

### PS5

The PS5 archives live under `.deps/llama-ps5` and are built from the same pinned
llama.cpp source with the PS5 payload SDK toolchain. The build uses:

- `toolchain/prospero.cmake`
- `BUILD_SHARED_LIBS=OFF`
- `LLAMA_BUILD_SERVER=ON`
- `LLAMA_BUILD_TESTS=OFF`, `LLAMA_BUILD_EXAMPLES=OFF`
- `LLAMA_BUILD_UI=OFF`, `LLAMA_USE_PREBUILT_UI=OFF`
- `LLAMA_OPENSSL=OFF`
- `GGML_NATIVE=OFF`, `GGML_OPENMP=OFF`
- `GGML_AVX=ON`, `GGML_AVX2=ON`, `GGML_FMA=ON`, `GGML_F16C=ON`,
  `GGML_BMI2=ON`
- `-DSUBPROCESS_HAVE_CWD=0`

The PS5 source needs the small PS5 compatibility patches used by the llama.cpp
port (thread affinity, safe default thread count, and child process handling).
`platform/ps5/Makefile` links the resulting archives with
`-Wl,--start-group ... -Wl,--end-group` so the static archives resolve each
other.

`platform/ps5/console_compat.cpp` supplies `accept4`, which the SDK headers
declare but libkernel does not export. It also keeps the existing fcntl socket
workaround for console libcurl.

The local server thread uses an explicit 16 MiB pthread stack. The console's
default stack is too small for llama.cpp startup. PS5LM uses the same size for
the same reason.

## What was measured

On the test console, with the model at
`/data/homebrew/PHAR00001/models/Qwen3.5-0.8B-Q4_K_M.gguf`:

- The harness started the in-process server and logged
  `local model ready`.
- A cold `/api/chat` answer (`console local model works.`) took 32.0 seconds.
- A tool turn that called `fs_list` on `/data/homebrew` took 14.5 seconds and
  returned an answer based on the real directory listing.
- The turn used 2 steps and 5,079 tokens, with no cloud key and no request to a
  cloud model provider.
- The log recorded `tool fs_list {"path":"/data/homebrew"}` and
  `provider served model local`.

These are single-run observations, not a benchmark. Prompt length, context
warmth and console load change the result. The first turn is slower because the
prompt cache is cold.

## Limits

- The 0.8B model is useful for short tool-driven turns. It is not a replacement
  for a large cloud model on long reasoning tasks.
- Four thousand tokens of context is a deliberate memory limit.
- The model must fit in console memory together with the harness and the
  browser. A larger quantisation or context can fail to allocate.
- The harness is still a userland process. It does not survive a reboot or a
  lost jailbreak and it is not a background service.
- The model file has its own licence. Check the model card before
  redistributing it.

## Third-party code

llama.cpp, ggml, cpp-httplib, nlohmann/json and the bundled hash code are
statically linked into the PS5 binary. Their licences are listed in
[`THIRD-PARTY-NOTICES.md`](../THIRD-PARTY-NOTICES.md) and included under
`licenses/` in the release bundle.
