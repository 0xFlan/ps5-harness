#include "ps5h/policy.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ps5h {
namespace {

std::string lowercase(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (unsigned char c : value) {
    out.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c));
  }
  return out;
}

bool is_loopback_address(const std::string& ip) {
  in_addr v4{};
  if (::inet_pton(AF_INET, ip.c_str(), &v4) == 1) {
    return ((ntohl(v4.s_addr) >> 24) & 0xFF) == 127;
  }
  in6_addr v6{};
  if (::inet_pton(AF_INET6, ip.c_str(), &v6) == 1) {
    return IN6_IS_ADDR_LOOPBACK(&v6) != 0;
  }
  return false;
}

bool is_ip_literal(const std::string& host, bool* is_v6) {
  in_addr v4{};
  if (::inet_pton(AF_INET, host.c_str(), &v4) == 1) {
    if (is_v6 != nullptr) *is_v6 = false;
    return true;
  }
  in6_addr v6{};
  if (::inet_pton(AF_INET6, host.c_str(), &v6) == 1) {
    if (is_v6 != nullptr) *is_v6 = true;
    return true;
  }
  return false;
}

bool ipv4_private(std::uint32_t address) {
  const std::uint32_t a = (address >> 24) & 0xFF;
  const std::uint32_t b = (address >> 16) & 0xFF;
  if (a == 0 || a == 10 || a == 127 || a == 255) return true;
  if (a == 169 && b == 254) return true;
  if (a == 172 && b >= 16 && b <= 31) return true;
  if (a == 192 && b == 168) return true;
  if (a == 100 && b >= 64 && b <= 127) return true;
  if (a == 192 && b == 0) return true;
  if (a == 198 && (b == 18 || b == 19)) return true;
  return false;
}

}  // namespace

bool is_private_address(const std::string& ip) {
  in_addr v4{};
  if (::inet_pton(AF_INET, ip.c_str(), &v4) == 1) {
    return ipv4_private(ntohl(v4.s_addr));
  }
  in6_addr v6{};
  if (::inet_pton(AF_INET6, ip.c_str(), &v6) == 1) {
    const std::uint8_t* b = v6.s6_addr;
    bool all_zero_until_last = true;
    for (int i = 0; i < 15; ++i) {
      if (b[i] != 0) {
        all_zero_until_last = false;
        break;
      }
    }
    if (all_zero_until_last && b[15] == 1) return true;  // ::1
    if (all_zero_until_last && b[15] == 0) return true;  // ::
    if ((b[0] & 0xFE) == 0xFC) return true;              // fc00::/7
    if (b[0] == 0xFE && (b[1] & 0xC0) == 0x80) return true;  // fe80::/10
    if (b[0] == 0xFF) return true;                        // multicast
    // IPv4-mapped ::ffff:a.b.c.d
    bool mapped = true;
    for (int i = 0; i < 10; ++i) {
      if (b[i] != 0) {
        mapped = false;
        break;
      }
    }
    if (mapped && b[10] == 0xFF && b[11] == 0xFF) {
      std::uint32_t value = (static_cast<std::uint32_t>(b[12]) << 24) |
                            (static_cast<std::uint32_t>(b[13]) << 16) |
                            (static_cast<std::uint32_t>(b[14]) << 8) | b[15];
      return ipv4_private(value);
    }
  }
  return false;
}

bool parse_url(const std::string& url, ParsedUrl* out) {
  if (out == nullptr) {
    return false;
  }
  std::size_t scheme_end = url.find("://");
  if (scheme_end == std::string::npos || scheme_end == 0) {
    return false;
  }
  out->scheme = lowercase(url.substr(0, scheme_end));
  std::string rest = url.substr(scheme_end + 3);
  std::size_t authority_end = rest.find_first_of("/?#");
  std::string authority = authority_end == std::string::npos
                              ? rest
                              : rest.substr(0, authority_end);
  out->path = authority_end == std::string::npos ? "/" : rest.substr(authority_end);
  if (authority.empty()) {
    return false;
  }

  std::size_t at = authority.rfind('@');
  if (at != std::string::npos) {
    authority = authority.substr(at + 1);
  }

  if (authority.front() == '[') {
    std::size_t close = authority.find(']');
    if (close == std::string::npos) {
      return false;
    }
    out->host = authority.substr(1, close - 1);
    if (close + 1 < authority.size() && authority[close + 1] == ':') {
      out->port = authority.substr(close + 2);
    }
  } else {
    std::size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
      out->host = authority.substr(0, colon);
      out->port = authority.substr(colon + 1);
    } else {
      out->host = authority;
    }
  }
  if (out->host.empty()) {
    return false;
  }
  out->host = lowercase(out->host);
  return true;
}

UrlPolicy::Decision UrlPolicy::check(const std::string& url) const {
  Decision decision;

  // Scheme is decided before authority parsing so `file:///etc/passwd` reports
  // the scheme refusal rather than a generic malformed-URL error.
  const std::size_t scheme_end = url.find("://");
  if (scheme_end == std::string::npos || scheme_end == 0) {
    decision.reason = "not an absolute URL";
    return decision;
  }
  const std::string scheme = lowercase(url.substr(0, scheme_end));
#if defined(PS5H_TEST_HOOKS)
  const bool cleartext_hook = test_allow_loopback_cleartext;
#else
  const bool cleartext_hook = false;
#endif

  // The strict path still reports the scheme (not a generic parse failure) so
  // `file:///etc/passwd` reads as a scheme refusal.
  if (https_only && scheme != "https" && !cleartext_hook) {
    decision.reason = "scheme must be https, got \"" + scheme + "\"";
    return decision;
  }
  if (!https_only && scheme != "https" && scheme != "http" && !cleartext_hook) {
    decision.reason = "unsupported scheme \"" + scheme + "\"";
    return decision;
  }

  ParsedUrl parsed;
  if (!parse_url(url, &parsed)) {
    decision.reason = "malformed URL";
    return decision;
  }

  // The test hook is narrow on purpose: loopback only, cleartext only.
  const bool loopback_host =
      parsed.host == "localhost" || is_loopback_address(parsed.host);
  const bool loopback_override = cleartext_hook && loopback_host;
  if (cleartext_hook && scheme != "https" && !loopback_host) {
    decision.reason = "scheme must be https, got \"" + scheme + "\"";
    return decision;
  }

  if (!allowed_hosts.empty()) {
    bool matched = false;
    for (const auto& entry : allowed_hosts) {
      if (entry == parsed.host) {
        matched = true;
        break;
      }
      if (!entry.empty() && entry.front() == '.' &&
          parsed.host.size() > entry.size() &&
          parsed.host.compare(parsed.host.size() - entry.size(), entry.size(), entry) == 0) {
        matched = true;
        break;
      }
    }
    if (!matched) {
      decision.reason = "host \"" + parsed.host + "\" is not in allowed_hosts";
      return decision;
    }
  }

  if (is_ip_literal(parsed.host, nullptr)) {
    if (deny_private_ranges && !loopback_override && is_private_address(parsed.host)) {
      decision.reason = "host \"" + parsed.host + "\" is a private or loopback address";
      return decision;
    }
    decision.allowed = true;
    decision.resolved_ip = parsed.host;
    return decision;
  }

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* results = nullptr;
  const std::string service = parsed.port.empty() ? (parsed.scheme == "https" ? "443" : "80")
                                                  : parsed.port;
  if (::getaddrinfo(parsed.host.c_str(), service.c_str(), &hints, &results) != 0) {
    decision.reason = "cannot resolve host \"" + parsed.host + "\"";
    return decision;
  }

  bool any = false;
  bool blocked = false;
  std::string first_ip;
  std::string blocked_ip;
  for (addrinfo* entry = results; entry != nullptr; entry = entry->ai_next) {
    char buffer[INET6_ADDRSTRLEN] = {0};
    const void* address = nullptr;
    if (entry->ai_family == AF_INET) {
      address = &reinterpret_cast<sockaddr_in*>(entry->ai_addr)->sin_addr;
    } else if (entry->ai_family == AF_INET6) {
      address = &reinterpret_cast<sockaddr_in6*>(entry->ai_addr)->sin6_addr;
    } else {
      continue;
    }
    if (::inet_ntop(entry->ai_family, address, buffer, sizeof buffer) == nullptr) {
      continue;
    }
    any = true;
    if (first_ip.empty()) {
      first_ip = buffer;
    }
    if (deny_private_ranges && !loopback_override && is_private_address(buffer)) {
      blocked = true;
      if (blocked_ip.empty()) {
        blocked_ip = buffer;
      }
    }
  }
  ::freeaddrinfo(results);

  if (!any) {
    decision.reason = "host \"" + parsed.host + "\" resolved to no usable address";
    return decision;
  }
  if (blocked) {
    decision.reason = "host \"" + parsed.host + "\" resolves to private address " + blocked_ip;
    return decision;
  }
  decision.allowed = true;
  decision.resolved_ip = first_ip;
  return decision;
}

#if defined(PS5H_TEST_HOOKS)
UrlPolicy UrlPolicy::test_only_loopback_policy() {
  UrlPolicy policy;
  policy.test_allow_loopback_cleartext = true;
  return policy;
}
#endif

}  // namespace ps5h
