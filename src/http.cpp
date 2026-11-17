#include "ps5h/core.h"
#include "ps5h/policy.h"
#include "ps5h/sha256.h"

#include <arpa/inet.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <string>

#if defined(PS5H_HTTP_STUB)

namespace ps5h {

HttpFn make_http_client() {
  return [](const HttpRequest&) {
    HttpResponse response;
    response.error = "HTTP backend not compiled in (build without PS5H_HTTP_STUB)";
    return response;
  };
}

DownloadFn make_download_client() {
  return [](const DownloadRequest&, ProgressFn, CancelFn) {
    DownloadResult result;
    result.error = "HTTP backend not compiled in (build without PS5H_HTTP_STUB)";
    return result;
  };
}

}  // namespace ps5h

#else

#include <curl/curl.h>

namespace ps5h {
namespace {

void ensure_curl_initialised() {
  static bool done = false;
  if (!done) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    done = true;
  }
}

curl_slist* apply_common(CURL* curl, const HttpRequest& request, curl_slist* headers) {
  curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "ps5-harness/0.1");
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.timeout_seconds > 0 ? request.timeout_seconds : 60L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, request.follow_redirects ? 1L : 0L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
  curl_slist* resolve = nullptr;
  if (!request.resolved_ip.empty()) {
    ParsedUrl parsed;
    if (parse_url(request.url, &parsed)) {
      // CURLOPT_RESOLVE maps a host name to an address. An IP literal has no
      // name to pin, and curl rejects the entry.
      in_addr literal4{};
      in6_addr literal6{};
      const bool literal = ::inet_pton(AF_INET, parsed.host.c_str(), &literal4) == 1 ||
                           ::inet_pton(AF_INET6, parsed.host.c_str(), &literal6) == 1;
      if (!literal) {
        const std::string port = parsed.port.empty() ? "443" : parsed.port;
        const std::string entry = parsed.host + ":" + port + ":" + request.resolved_ip;
        resolve = curl_slist_append(resolve, entry.c_str());
        curl_easy_setopt(curl, CURLOPT_RESOLVE, resolve);
      }
    }
  }
  if (headers != nullptr) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  }
  return resolve;
}

struct BodySink {
  std::string* out;
  std::size_t limit;
  bool overflow;
};

std::size_t write_callback(char* data, std::size_t size, std::size_t count, void* user) {
  auto* sink = static_cast<BodySink*>(user);
  const std::size_t total = size * count;
  if (sink->limit > 0 && sink->out->size() + total > sink->limit) {
    sink->overflow = true;
    return 0;  // aborts the transfer
  }
  sink->out->append(data, total);
  return total;
}

struct TransferState {
  ProgressFn progress;
  CancelFn cancel;
  unsigned long long max_bytes;
  unsigned long long received;
  bool cancelled;
  bool over_limit;
};

int xferinfo_callback(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  auto* state = static_cast<TransferState*>(user);
  if (state->cancel && state->cancel()) {
    state->cancelled = true;
    return 1;
  }
  if (state->max_bytes > 0 && state->received > state->max_bytes) {
    state->over_limit = true;
    return 1;
  }
  return 0;
}

}  // namespace

HttpFn make_http_client() {
  ensure_curl_initialised();

  return [](const HttpRequest& request) -> HttpResponse {
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
      response.error = "curl_easy_init failed";
      return response;
    }

    curl_slist* headers = nullptr;
    for (const auto& header : request.headers) {
      headers = curl_slist_append(headers, (header.first + ": " + header.second).c_str());
    }

    BodySink sink{&response.body, request.max_body_bytes, false};
    curl_slist* resolve = apply_common(curl, request, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);

    if (request.method == "POST") {
      curl_easy_setopt(curl, CURLOPT_POST, 1L);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    } else if (request.method != "GET") {
      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
    }

    CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
      if (sink.overflow) {
        response.error = "response exceeds " + std::to_string(request.max_body_bytes) + " bytes";
      } else {
        response.error = curl_easy_strerror(code);
      }
    } else {
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    }

    if (resolve != nullptr) curl_slist_free_all(resolve);
    if (headers != nullptr) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
  };
}

DownloadFn make_download_client() {
  ensure_curl_initialised();

  return [](const DownloadRequest& request, ProgressFn progress,
            CancelFn cancel) -> DownloadResult {
    DownloadResult result;
    const std::string part = request.path + ".part";

    unsigned long long resume_from = 0;
    struct stat existing {};
    if (request.resume && ::stat(part.c_str(), &existing) == 0 &&
        static_cast<unsigned long long>(existing.st_size) > 0) {
      resume_from = static_cast<unsigned long long>(existing.st_size);
    }

    FILE* file = std::fopen(part.c_str(), resume_from > 0 ? "ab" : "wb");
    if (file == nullptr) {
      result.error = "cannot open for writing: " + part;
      return result;
    }

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
      std::fclose(file);
      result.error = "curl_easy_init failed";
      return result;
    }

    curl_slist* headers = nullptr;
    for (const auto& header : request.request.headers) {
      headers = curl_slist_append(headers, (header.first + ": " + header.second).c_str());
    }
    curl_slist* resolve = apply_common(curl, request.request, headers);

    TransferState state{progress, cancel, request.max_bytes, resume_from, false, false};
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                     +[](char* data, std::size_t size, std::size_t count, void* user) -> std::size_t {
                       auto* out = static_cast<FILE*>(user);
                       return std::fwrite(data, size, count, out) * size;
                     });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
    curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(resume_from));

    CURLcode code = curl_easy_perform(curl);

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    result.status = status;

    curl_off_t downloaded = 0;
    curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &downloaded);
    result.bytes = resume_from + static_cast<unsigned long long>(downloaded > 0 ? downloaded : 0);

    if (resolve != nullptr) curl_slist_free_all(resolve);
    if (headers != nullptr) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    std::fflush(file);
    std::fclose(file);

    if (state.cancelled) {
      result.error = "cancelled";
      return result;
    }
    if (state.over_limit) {
      result.error = "download exceeds the configured byte limit";
      return result;
    }
    if (code != CURLE_OK) {
      result.error = curl_easy_strerror(code);
      return result;
    }
    if (status < 200 || status >= 300) {
      result.error = "HTTP " + std::to_string(status);
      return result;
    }

    if (!request.expected_sha256.empty()) {
      result.sha256 = sha256_file_hex(part);
      if (result.sha256 != request.expected_sha256) {
        result.error = "sha256 mismatch";
        return result;
      }
    }
    if (std::rename(part.c_str(), request.path.c_str()) != 0) {
      result.error = "cannot rename into place: " + request.path;
      return result;
    }
    result.ok = true;
    return result;
  };
}

}  // namespace ps5h

#endif
