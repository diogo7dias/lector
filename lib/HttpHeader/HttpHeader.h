#pragma once

#include <cstddef>
#include <string>
#include <string_view>

// One extra header sent with every request to a server, on top of the auth and
// user-agent headers the transport sets itself (for example a Cloudflare Access
// service token). The value is a credential: stored obfuscated, never logged,
// never shown.
struct HttpHeader {
  std::string name;
  std::string value;
};

constexpr size_t HTTP_HEADER_NAME_MAX = 63;
constexpr size_t HTTP_HEADER_VALUE_MAX = 511;

// "Name: Value" -> trimmed name and value. A line without a colon is a bare
// name with no value.
void parseHttpHeaderLine(std::string_view line, HttpHeader& header);

// Inverse of parseHttpHeaderLine(); empty for an unused slot. With maskValue a
// non-empty value shows as "******".
std::string formatHttpHeaderLine(const HttpHeader& header, bool maskValue = false);

// True when the header may go on the wire. The name must be an HTTP token, and
// not one the transport owns (Host, Content-Length, ...): the request line is
// built from raw text, so a CR, LF or control character in either part would
// split it. An empty name is an unused slot and needs an empty value.
bool isValidHttpHeader(std::string_view name, std::string_view value);

// Case-insensitive name comparison.
bool isHttpHeaderNamed(std::string_view name, std::string_view wanted);
