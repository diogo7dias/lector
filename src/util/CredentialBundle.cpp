#include "CredentialBundle.h"

#include <cctype>
#include <cstdio>

namespace credential_bundle {
namespace {

// The bundle is written and read here and nowhere else, and the host tests build
// without ArduinoJson, so the format is handled directly. The schema is flat --
// two arrays of objects whose every value is a string -- which is small enough to
// read exactly rather than approximately.

void appendEscaped(std::string& out, const std::string& value) {
  out += '"';
  for (const char c : value) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          out += buf;
        } else {
          out += c;
        }
    }
  }
  out += '"';
}

void appendField(std::string& out, const char* key, const std::string& value, const bool last) {
  out += '"';
  out += key;
  out += "\":";
  appendEscaped(out, value);
  if (!last) out += ',';
}

class Reader {
 public:
  explicit Reader(const std::string& text) : text_(text) {}

  void skipSpace() {
    while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) pos_++;
  }

  bool take(const char expected) {
    skipSpace();
    if (pos_ >= text_.size() || text_[pos_] != expected) return false;
    pos_++;
    return true;
  }

  bool peek(const char expected) {
    skipSpace();
    return pos_ < text_.size() && text_[pos_] == expected;
  }

  bool atEnd() {
    skipSpace();
    return pos_ >= text_.size();
  }

  bool readString(std::string& out) {
    skipSpace();
    if (pos_ >= text_.size() || text_[pos_] != '"') return false;
    pos_++;
    out.clear();
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if (c == '"') return true;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (pos_ >= text_.size()) return false;
      const char esc = text_[pos_++];
      switch (esc) {
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case 't':
          out += '\t';
          break;
        case 'u': {
          // Only the escapes this writer emits, which are control characters, so
          // the code point always fits in one byte. Anything else is refused
          // rather than half-decoded.
          if (pos_ + 4 > text_.size()) return false;
          int value = 0;
          for (int i = 0; i < 4; i++) {
            const char h = text_[pos_++];
            const int digit = (h >= '0' && h <= '9')   ? h - '0'
                              : (h >= 'a' && h <= 'f') ? h - 'a' + 10
                              : (h >= 'A' && h <= 'F') ? h - 'A' + 10
                                                       : -1;
            if (digit < 0) return false;
            value = value * 16 + digit;
          }
          if (value > 0x7F) return false;
          out += static_cast<char>(value);
          break;
        }
        default:
          out += esc;
      }
    }
    return false;
  }

  /** Reads a bare number. The only one in the schema is the version. */
  bool readInt(int& out) {
    skipSpace();
    const size_t start = pos_;
    if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) pos_++;
    while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) pos_++;
    if (pos_ == start) return false;
    out = std::atoi(text_.substr(start, pos_ - start).c_str());
    return true;
  }

  /** Steps over a value this schema does not know, so an older reader is not stuck. */
  bool skipValue() {
    skipSpace();
    if (pos_ >= text_.size()) return false;
    const char c = text_[pos_];
    if (c == '"') {
      std::string ignored;
      return readString(ignored);
    }
    if (c == '{' || c == '[') {
      const char open = c;
      const char close = c == '{' ? '}' : ']';
      int depth = 0;
      while (pos_ < text_.size()) {
        const char here = text_[pos_];
        if (here == '"') {
          std::string ignored;
          if (!readString(ignored)) return false;
          continue;
        }
        pos_++;
        if (here == open) depth++;
        if (here == close && --depth == 0) return true;
      }
      return false;
    }
    while (pos_ < text_.size() && text_[pos_] != ',' && text_[pos_] != '}' && text_[pos_] != ']') pos_++;
    return true;
  }

 private:
  const std::string& text_;
  size_t pos_ = 0;
};

/** Reads one flat object of string fields, handing each to `assign`. */
template <typename Assign>
bool readObject(Reader& reader, const Assign& assign) {
  if (!reader.take('{')) return false;
  if (reader.take('}')) return true;
  while (true) {
    std::string key;
    if (!reader.readString(key)) return false;
    if (!reader.take(':')) return false;
    if (reader.peek('"')) {
      std::string value;
      if (!reader.readString(value)) return false;
      assign(key, value);
    } else if (!reader.skipValue()) {
      return false;
    }
    if (reader.take(',')) continue;
    return reader.take('}');
  }
}

}  // namespace

std::string serialize(const Bundle& bundle) {
  std::string out;
  out.reserve(256);
  out += "{\"version\":";
  out += std::to_string(FORMAT_VERSION);
  out += ",\"wifi\":[";
  for (size_t i = 0; i < bundle.wifi.size(); i++) {
    if (i) out += ',';
    out += '{';
    appendField(out, "ssid", bundle.wifi[i].ssid, false);
    appendField(out, "password", bundle.wifi[i].password, true);
    out += '}';
  }
  out += "],\"opds\":[";
  for (size_t i = 0; i < bundle.opds.size(); i++) {
    if (i) out += ',';
    out += '{';
    appendField(out, "name", bundle.opds[i].name, false);
    appendField(out, "url", bundle.opds[i].url, false);
    appendField(out, "username", bundle.opds[i].username, false);
    appendField(out, "password", bundle.opds[i].password, true);
    out += '}';
  }
  out += "]}";
  return out;
}

bool parse(const std::string& json, Bundle& out) {
  out.wifi.clear();
  out.opds.clear();

  Reader reader(json);
  if (!reader.take('{')) return false;
  bool sawVersion = false;

  if (reader.take('}')) return false;  // an empty document promises nothing

  while (true) {
    std::string key;
    if (!reader.readString(key)) return false;
    if (!reader.take(':')) return false;

    if (key == "version") {
      int version = 0;
      if (!reader.readInt(version)) return false;
      // A bundle from a firmware that knows more than this one is refused rather
      // than read with fields missing: half a login is worse than none.
      if (version != FORMAT_VERSION) return false;
      sawVersion = true;
    } else if (key == "wifi" || key == "opds") {
      const bool isWifi = key == "wifi";
      if (!reader.take('[')) return false;
      if (!reader.take(']')) {
        while (true) {
          if (isWifi) {
            WifiEntry entry;
            if (!readObject(reader, [&entry](const std::string& field, const std::string& value) {
                  if (field == "ssid") entry.ssid = value;
                  if (field == "password") entry.password = value;
                }))
              return false;
            if (!entry.ssid.empty() && out.wifi.size() < MAX_ENTRIES) out.wifi.push_back(std::move(entry));
          } else {
            OpdsEntry entry;
            if (!readObject(reader, [&entry](const std::string& field, const std::string& value) {
                  if (field == "name") entry.name = value;
                  if (field == "url") entry.url = value;
                  if (field == "username") entry.username = value;
                  if (field == "password") entry.password = value;
                }))
              return false;
            if (!entry.url.empty() && out.opds.size() < MAX_ENTRIES) out.opds.push_back(std::move(entry));
          }
          if (reader.take(',')) continue;
          if (!reader.take(']')) return false;
          break;
        }
      }
    } else if (!reader.skipValue()) {
      return false;
    }

    if (reader.take(',')) continue;
    if (!reader.take('}')) return false;
    break;
  }

  return sawVersion;
}

bool isBundleFilename(const std::string& name) {
  const std::string extension = FILE_EXTENSION;
  if (name.size() <= extension.size()) return false;
  const size_t start = name.size() - extension.size();
  for (size_t i = 0; i < extension.size(); i++) {
    if (std::tolower(static_cast<unsigned char>(name[start + i])) != extension[i]) return false;
  }
  return true;
}

}  // namespace credential_bundle
