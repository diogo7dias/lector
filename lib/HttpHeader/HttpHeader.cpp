#include "HttpHeader.h"

#include <AsciiText.h>

#include <algorithm>
#include <cctype>

namespace {

// Headers the HTTP client writes itself; a second copy would be rejected by
// strict servers or change how the body is framed.
constexpr std::string_view RESERVED[] = {"host",       "content-length", "content-type", "transfer-encoding",
                                         "connection", "keep-alive",     "upgrade",      "te",
                                         "trailer",    "expect",         "range",        "user-agent"};

bool isTokenChar(const char c) {
  return std::isalnum(static_cast<unsigned char>(c)) ||
         std::string_view("!#$%&'*+-.^_`|~").find(c) != std::string_view::npos;
}

}  // namespace

bool isHttpHeaderNamed(const std::string_view name, const std::string_view wanted) {
  return name.size() == wanted.size() && std::equal(name.begin(), name.end(), wanted.begin(), [](char a, char b) {
           return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
         });
}

void parseHttpHeaderLine(const std::string_view line, HttpHeader& header) {
  const size_t colon = line.find(':');
  header.name = trimAsciiWhitespace(line.substr(0, colon));
  if (colon == std::string_view::npos) {
    header.value.clear();
  } else {
    header.value = trimAsciiWhitespace(line.substr(colon + 1));
  }
}

void parseHttpHeaderEdit(const std::string_view line, const HttpHeader& saved, HttpHeader& header) {
  const std::string keptValue = saved.value;  // `header` may be `saved`
  const std::string savedName = saved.name;
  parseHttpHeaderLine(line, header);
  if (header.value.empty() && !header.name.empty() && isHttpHeaderNamed(header.name, savedName)) {
    header.value = keptValue;
  }
}

std::string formatHttpHeaderLine(const HttpHeader& header, const bool maskValue) {
  if (header.name.empty()) return "";
  if (header.value.empty()) return header.name;
  return header.name + ": " + (maskValue ? "******" : header.value);
}

bool isValidHttpHeader(const std::string_view name, const std::string_view value) {
  if (value.size() > HTTP_HEADER_VALUE_MAX) return false;
  // Printable ASCII and tab only: anything else, CR and LF included, can end the header early.
  if (!std::all_of(value.begin(), value.end(), [](char c) { return c == '\t' || (c >= ' ' && c <= '~'); }))
    return false;
  if (name.empty()) return value.empty();
  if (name.size() > HTTP_HEADER_NAME_MAX || !std::all_of(name.begin(), name.end(), isTokenChar)) return false;
  return std::none_of(std::begin(RESERVED), std::end(RESERVED),
                      [name](std::string_view reserved) { return isHttpHeaderNamed(name, reserved); });
}
