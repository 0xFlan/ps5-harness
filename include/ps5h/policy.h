#pragma once

#include <string>
#include <vector>

namespace ps5h {

// Network policy for manifests and any URL the agent hands to a tool (A4).
//
// A manifest is a stranger's JSON. Without this policy, `file://` turns one into
// a local file reader, `http://` silently downgrades, and a LAN address turns
// the console into an SSRF probe of the user's own network.
struct UrlPolicy {
  bool https_only = true;
  bool deny_private_ranges = true;
  // Exact host names, or entries starting with '.' for a suffix match.
  std::vector<std::string> allowed_hosts;

  struct Decision {
    bool allowed = false;
    std::string reason;
    // First resolved address, for CURLOPT_RESOLVE pinning. Empty when the host
    // is not resolvable locally (the request is then refused).
    std::string resolved_ip;
  };

  Decision check(const std::string& url) const;

#if defined(PS5H_TEST_HOOKS)
  // TEST ONLY. Never present in a production build: this member and the factory
  // below are compiled out unless PS5H_TEST_HOOKS is defined, which only the
  // test binaries do. Config parsing never touches it, so no config file can
  // enable it. It permits cleartext HTTP to a loopback host only, which is what
  // the local provider stub needs.
  bool test_allow_loopback_cleartext = false;

  static UrlPolicy test_only_loopback_policy();
#endif
};

// Minimal absolute-URL parser: enough for policy checks, not a general RFC 3986
// implementation.
struct ParsedUrl {
  std::string scheme;
  std::string host;
  std::string port;
  std::string path;
};

bool parse_url(const std::string& url, ParsedUrl* out);

// True for loopback, private, link-local, CGNAT and unspecified addresses.
bool is_private_address(const std::string& ip);

}  // namespace ps5h
