# Third-party notices

The PS5 release binary (`PHAR00001/harness.elf`) is statically linked with the
following libraries from the PacBrew v0.41.1 image. Their licence texts are
included in full in `licenses/` and are covered by `SHA256SUMS`.

| Component | Version | Licence | Full text |
|---|---|---|---|
| libcurl | 8.18.0 | curl licence (MIT/X derivative) | `licenses/curl-COPYING.txt` |
| OpenSSL | 3.5.2 | Apache License 2.0 | `licenses/openssl-LICENSE.txt` |
| zlib | 1.3.2 | zlib licence | `licenses/zlib-LICENSE.txt` |
| zstd | 1.5.6 | BSD 3-Clause | `licenses/zstd-LICENSE.txt` |
| libpsl | 0.21.5 | MIT | `licenses/libpsl-COPYING.txt` |
| Public Suffix List data | bundled by libpsl | Mozilla Public License 2.0 | `licenses/public-suffix-list-LICENSE.txt` |

The source texts were taken from the upstream release tags:

- <https://github.com/curl/curl/blob/curl-8_18_0/COPYING>
- <https://github.com/openssl/openssl/blob/openssl-3.5.2/LICENSE.txt>
- <https://github.com/madler/zlib/blob/v1.3.2/LICENSE>
- <https://github.com/facebook/zstd/blob/v1.5.6/LICENSE>
- <https://github.com/rockdaboot/libpsl/blob/0.21.5/COPYING>
- <https://github.com/publicsuffix/list/blob/master/LICENSE>

The harness's own source is licensed under the MIT licence in `LICENSE`. These
notices do not change the licence of the harness or of any dependency.
