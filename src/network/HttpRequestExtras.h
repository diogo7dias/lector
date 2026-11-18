#pragma once

#include <HttpHeader.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * What a request may carry besides a username and password. Every field is a
 * credential, so like them it goes only to the origin the transfer started on:
 * never to a redirect target on another host, and never from https down to http.
 */
struct HttpRequestExtras {
  // Sent as "Authorization: Bearer ..."; replaces Basic when set.
  std::string bearer;
  // Extra request headers. A header named Authorization replaces both of the above.
  std::vector<std::pair<std::string, std::string>> headers;
};

namespace request_headers {

using Line = std::pair<std::string, std::string>;

/**
 * The credential headers for one request, in the order they go on the wire: the extra
 * headers that are valid, then one Authorization unless a custom header already is one
 * (a custom Authorization wins, then the bearer token, then `basic`, the whole
 * "Basic ..." value or empty). Pure, so the rules run on the host.
 */
inline std::vector<Line> build(const HttpRequestExtras& extras, const std::string_view basic) {
  std::vector<Line> lines;
  lines.reserve(extras.headers.size() + 1);
  bool authorization = false;
  for (const auto& [name, value] : extras.headers) {
    if (!isValidHttpHeader(name, value)) continue;
    lines.emplace_back(name, value);
    authorization = authorization || isHttpHeaderNamed(name, "authorization");
  }
  if (authorization) return lines;
  if (!extras.bearer.empty()) {
    lines.emplace_back("Authorization", "Bearer " + extras.bearer);
  } else if (!basic.empty()) {
    lines.emplace_back("Authorization", std::string(basic));
  }
  return lines;
}

}  // namespace request_headers
