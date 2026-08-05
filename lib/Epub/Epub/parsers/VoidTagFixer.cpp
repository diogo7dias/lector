#include "VoidTagFixer.h"

#include <cstring>

namespace VoidTagFixer {
namespace {

// The HTML void elements. Kept as a flat table so it stays in flash and costs no RAM.
constexpr const char* VOID_ELEMENTS[] = {"area",  "base", "br",   "col",   "embed",  "hr",    "img",
                                         "input", "link", "meta", "param", "source", "track", "wbr"};

bool equalsIgnoreCase(const char* a, const size_t aLen, const char* b) {
  size_t i = 0;
  for (; i < aLen && b[i] != '\0'; i++) {
    char x = a[i];
    char y = b[i];
    if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
    if (x != y) return false;
  }
  return i == aLen && b[i] == '\0';
}

// Scans forward from the '<' at `start` for the '>' that ends the tag, skipping any '>' that
// sits inside a quoted attribute value. Returns len when the tag does not finish in this chunk.
size_t findTagEnd(const char* buf, const size_t start, const size_t len) {
  char quote = '\0';
  for (size_t i = start; i < len; i++) {
    const char c = buf[i];
    if (quote != '\0') {
      if (c == quote) quote = '\0';
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
      continue;
    }
    if (c == '>') return i;
  }
  return len;
}

// Finds `needle` at or after `from`. Returns len when absent.
size_t findSequence(const char* buf, const size_t from, const size_t len, const char* needle) {
  const size_t nLen = strlen(needle);
  if (nLen == 0 || len < nLen) return len;
  for (size_t i = from; i + nLen <= len; i++) {
    if (memcmp(buf + i, needle, nLen) == 0) return i;
  }
  return len;
}

}  // namespace

bool isVoidElement(const char* name, const size_t len) {
  if (len == 0) return false;
  for (const char* candidate : VOID_ELEMENTS) {
    if (equalsIgnoreCase(name, len, candidate)) return true;
  }
  return false;
}

size_t process(char* buf, size_t len, const size_t cap, char* carry, size_t& carryLen, const bool atEof) {
  carryLen = 0;
  size_t i = 0;
  while (i < len) {
    if (buf[i] != '<') {
      i++;
      continue;
    }

    // Work out where this construct ends. Comments, CDATA and processing instructions have
    // their own terminators and must not be treated as tags.
    const size_t remaining = len - i;
    size_t end;
    bool isStartTag = false;
    if (remaining >= 4 && memcmp(buf + i, "<!--", 4) == 0) {
      end = findSequence(buf, i + 4, len, "-->");
      if (end != len) end += 2;  // land on the '>' of "-->"
    } else if (remaining >= 9 && memcmp(buf + i, "<![CDATA[", 9) == 0) {
      end = findSequence(buf, i + 9, len, "]]>");
      if (end != len) end += 2;
    } else if (remaining >= 2 && buf[i + 1] == '?') {
      end = findSequence(buf, i + 2, len, "?>");
      if (end != len) end += 1;
    } else if (remaining >= 2 && buf[i + 1] == '/') {
      end = findTagEnd(buf, i + 1, len);
    } else if (remaining < 4 && !atEof) {
      // Too short to classify yet (it could still become "<!--"); hold it back.
      end = len;
    } else {
      end = findTagEnd(buf, i + 1, len);
      isStartTag = true;
    }

    if (end >= len) {
      // The construct runs past this chunk. Hold it back for the next one, unless we are at
      // the end of the file or it is longer than the carry buffer, in which case pass it on
      // untouched and let expat judge it.
      if (!atEof && (len - i) <= MAX_CARRY) {
        carryLen = len - i;
        memcpy(carry, buf + i, carryLen);
        return i;
      }
      return len;
    }

    if (isStartTag) {
      // Already self-closing: leave it alone.
      if (buf[end - 1] != '/') {
        size_t nameStart = i + 1;
        size_t nameEnd = nameStart;
        while (nameEnd < end) {
          const char c = buf[nameEnd];
          if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '/' || c == '>') break;
          nameEnd++;
        }
        if (isVoidElement(buf + nameStart, nameEnd - nameStart) && len + 1 <= cap) {
          // Insert the '/' before the '>'. One byte per void element, and the caller sized
          // the buffer with room for it.
          memmove(buf + end + 1, buf + end, len - end);
          buf[end] = '/';
          len++;
          end++;
        }
      }
    }

    i = end + 1;
  }
  return len;
}

}  // namespace VoidTagFixer
