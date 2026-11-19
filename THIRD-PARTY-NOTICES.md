# Third-party notices

The PS5 release binary (`PHAR00001/harness.elf`) statically includes code from
the PacBrew v0.41.1 image and the PS5 payload SDK's LLVM C++ runtime. The full
licence texts below are included in the archive under `licenses/`, and every
one of those files is covered by `SHA256SUMS`.

| Component | Version | Licence | Full text |
|---|---|---|---|
| libcurl | 8.18.0 | curl licence (MIT/X derivative) | `licenses/curl-COPYING.txt` |
| OpenSSL | 3.5.2 | Apache License 2.0 | `licenses/openssl-LICENSE.txt` |
| zlib | 1.3.2 | zlib licence | `licenses/zlib-LICENSE.txt` |
| Zstandard | 1.5.6 | BSD 3-Clause | `licenses/zstd-LICENSE.txt` |
| xxHash | 0.8.2 (embedded in Zstandard) | BSD 2-Clause | `licenses/xxhash-LICENSE.txt` |
| libpsl | 0.21.5 | MIT | `licenses/libpsl-COPYING.txt` |
| Public Suffix List data | snapshot bundled by libpsl 0.21.5 | Mozilla Public License 2.0 | `licenses/public-suffix-list-LICENSE.txt` |
| LLVM libc++ | 18.1.0 | Apache-2.0 WITH LLVM-exception | `licenses/libcxx-LICENSE.txt` |
| LLVM libc++abi | 18.1.0 | Apache-2.0 WITH LLVM-exception | `licenses/libcxxabi-LICENSE.txt` |
| LLVM libunwind | 18.1.0 | Apache-2.0 WITH LLVM-exception | `licenses/libunwind-LICENSE.txt` |

The upstream texts were taken from the corresponding release tags:

- <https://github.com/curl/curl/blob/curl-8_18_0/COPYING>
- <https://github.com/openssl/openssl/blob/openssl-3.5.2/LICENSE.txt>
- <https://github.com/madler/zlib/blob/v1.3.2/LICENSE>
- <https://github.com/facebook/zstd/blob/v1.5.6/LICENSE>
- <https://github.com/Cyan4973/xxHash/blob/v0.8.2/LICENSE>
- <https://github.com/rockdaboot/libpsl/blob/0.21.5/COPYING>
- <https://github.com/publicsuffix/list/blob/master/LICENSE>
- <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.0/libcxx/LICENSE.TXT>
- <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.0/libcxxabi/LICENSE.TXT>
- <https://github.com/llvm/llvm-project/blob/llvmorg-18.1.0/libunwind/LICENSE.TXT>

The payload also has dynamic dependencies on Sony's `libkernel_web.sprx`,
`libSceLibcInternal.sprx` and `libSceNet.sprx`; those system libraries are not
redistributed in this archive. The SDK's `libdl.a` and `libpthread.a` are empty
8-byte linker stubs, so they add no separate code or notice.

The harness's own source is licensed under the MIT licence in `LICENSE`. These
notices do not change the licence of the harness or of any dependency.
