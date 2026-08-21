#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#include "util/StringUtils.h"

// Naming and validation rules for "fetch this URL onto the card", kept free of
// networking headers so they can be reasoned about (and tested) on their own.
// The device never trusts a remote name: everything that ends up as a filename
// goes through sanitizeFilename, which strips path separators, so a fetched
// file can only ever land inside the directory the request asked for.
namespace fetch_url {

// Name used when neither the URL nor the response offers a usable one.
inline constexpr const char* DEFAULT_FILENAME = "download";

namespace detail {

inline int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Percent-decoding only. '+' is left alone: it is a form-encoding convention,
// not a URL path one, and a book called "C++.epub" should keep its name.
inline std::string percentDecode(const std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == '%' && i + 2 < text.size()) {
      const int hi = hexValue(text[i + 1]);
      const int lo = hexValue(text[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out += static_cast<char>(hi * 16 + lo);
        i += 2;
        continue;
      }
    }
    out += text[i];
  }
  return out;
}

inline bool startsWithIgnoreCase(const std::string_view text, const std::string_view prefix) {
  if (text.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); i++) {
    if (std::tolower(static_cast<unsigned char>(text[i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) {
      return false;
    }
  }
  return true;
}

inline std::string_view trim(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
  return text;
}

inline std::string_view unquote(std::string_view text) {
  if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
    text.remove_prefix(1);
    text.remove_suffix(1);
  }
  return text;
}

// sanitizeFilename never returns an empty string (it substitutes "book"), so a
// name with nothing usable in it has to be rejected before it gets there: here
// an empty result is the signal that the source offered no usable name.
// Spaces, dots and control characters are exactly what sanitizeFilename itself
// discards, so a name made only of those would come back as the substitute.
inline std::string sanitizedOrEmpty(const std::string_view decoded) {
  const bool hasUsableCharacter = std::any_of(decoded.begin(), decoded.end(), [](const char c) {
    const auto byte = static_cast<unsigned char>(c);
    return byte >= 128 || (byte > ' ' && byte < 127 && c != '.');
  });
  if (!hasUsableCharacter) return "";
  return StringUtils::sanitizeFilename(std::string(decoded));
}

// End of the parameter starting at `pos`, honouring quoted strings so a ';'
// inside `filename="a;b.epub"` does not end it early (RFC 6266 allows it).
inline size_t parameterEnd(const std::string_view header, size_t pos) {
  bool inQuotes = false;
  for (; pos < header.size(); pos++) {
    if (header[pos] == '"') {
      inQuotes = !inQuotes;
    } else if (header[pos] == ';' && !inQuotes) {
      return pos;
    }
  }
  return header.size();
}

}  // namespace detail

// True for an absolute http(s) URL with a non-empty host. Other schemes are
// refused rather than handed to the HTTP client: file:// and ftp:// have no
// meaning here, and a scheme-less string is a typo, not a location.
inline bool isSupportedUrl(const std::string_view url) {
  std::string_view rest;
  if (detail::startsWithIgnoreCase(url, "http://")) {
    rest = url.substr(7);
  } else if (detail::startsWithIgnoreCase(url, "https://")) {
    rest = url.substr(8);
  } else {
    return false;
  }
  const size_t hostEnd = rest.find_first_of("/?#");
  return !rest.substr(0, hostEnd).empty();
}

// Last path segment of the URL, percent-decoded and sanitized. Query string and
// fragment are dropped: neither is part of the name, and a signed download link
// carries its whole credential there.
inline std::string filenameFromUrl(std::string_view url) {
  url = url.substr(0, url.find_first_of("?#"));

  // Skip the scheme so its "//" is not mistaken for a path separator.
  if (const size_t schemeEnd = url.find("://"); schemeEnd != std::string_view::npos) {
    url.remove_prefix(schemeEnd + 3);
  }
  // Everything before the first '/' is the host, which is never a filename.
  const size_t pathStart = url.find('/');
  if (pathStart == std::string_view::npos) return DEFAULT_FILENAME;

  const std::string_view segment = url.substr(url.find_last_of('/') + 1);
  const std::string name = detail::sanitizedOrEmpty(detail::percentDecode(segment));
  return name.empty() ? DEFAULT_FILENAME : name;
}

// Filename offered by a Content-Disposition header, or "" when it offers none.
// RFC 6266's extended `filename*` form wins over plain `filename`, since it is
// the one that can carry non-ASCII. Callers fall back to filenameFromUrl.
inline std::string filenameFromContentDisposition(const std::string_view header) {
  std::string plain;
  std::string extended;

  size_t pos = 0;
  while (pos <= header.size()) {
    const size_t end = detail::parameterEnd(header, pos);
    const std::string_view param = detail::trim(header.substr(pos, end - pos));
    pos = end + 1;

    if (!detail::startsWithIgnoreCase(param, "filename")) continue;
    std::string_view rest = detail::trim(param.substr(8));

    const bool isExtended = !rest.empty() && rest.front() == '*';
    if (isExtended) rest = detail::trim(rest.substr(1));
    if (rest.empty() || rest.front() != '=') continue;

    std::string_view value = detail::unquote(detail::trim(rest.substr(1)));
    if (isExtended) {
      // "UTF-8''<pct-encoded>" - the charset and language are dropped; anything
      // the device cannot represent is handled downstream by sanitizeFilename.
      if (const size_t quotes = value.rfind('\''); quotes != std::string_view::npos) {
        value = value.substr(quotes + 1);
      }
      extended = detail::sanitizedOrEmpty(detail::percentDecode(value));
    } else {
      plain = detail::sanitizedOrEmpty(detail::percentDecode(value));
    }
  }

  return !extended.empty() ? extended : plain;
}

// Absolute path a fetched file is written to. `directory` is the folder the
// request named; `filename` must already have been through the helpers above.
inline std::string destinationPath(const std::string_view directory, const std::string_view filename) {
  std::string path(directory);
  if (path.empty() || path.front() != '/') path.insert(path.begin(), '/');
  if (path.back() != '/') path += '/';
  path.append(filename);
  return path;
}

}  // namespace fetch_url
