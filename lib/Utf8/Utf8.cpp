#include "Utf8.h"

#include "Utf8ComposeTable.h"

namespace {
// Look up the canonical composition of (base + combining mark), or 0 if none.
uint32_t utf8ComposePair(const uint32_t base, const uint32_t mark) {
  if (base > 0xFFFF || mark > 0xFFFF) return 0;
  int lo = 0;
  int hi = kUtf8ComposeTableSize - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const Utf8ComposeEntry& e = kUtf8ComposeTable[mid];
    if (e.base < base || (e.base == base && e.mark < mark)) {
      lo = mid + 1;
    } else if (e.base > base || (e.base == base && e.mark > mark)) {
      hi = mid - 1;
    } else {
      return e.composed;
    }
  }
  return 0;
}
}  // namespace

std::string utf8ComposeNfc(const std::string& in) {
  // Fast path: NFC composition can only change text that contains a combining
  // diacritical mark U+0300-036F (UTF-8 lead byte 0xCC or 0xCD) or conjoining
  // Hangul jamo (lead byte 0xE1 for U+1000-1FFF). Plain ASCII and
  // already-precomposed (NFC) text -- the vast majority of words -- have none, so
  // return them untouched without walking codepoints or allocating. A 0xCD or
  // 0xE1 that is actually a non-composing codepoint (e.g. Georgian, Cherokee)
  // just falls through to the full pass below.
  bool maybeHasMarks = false;
  for (const unsigned char c : in) {
    if (c == 0xCC || c == 0xCD || c == 0xE1) {
      maybeHasMarks = true;
      break;
    }
  }
  if (!maybeHasMarks) return in;

  std::string out;
  out.reserve(in.size());
  const unsigned char* p = reinterpret_cast<const unsigned char*>(in.c_str());
  uint32_t base = 0;
  bool haveBase = false;
  while (*p) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0) break;
    if (utf8IsCombiningMark(cp)) {
      const uint32_t composed = haveBase ? utf8ComposePair(base, cp) : 0;
      if (composed) {
        base = composed;  // keep accumulating further marks onto the composed char
        continue;
      }
      // No composition: flush the pending base, then emit the mark unchanged.
      if (haveBase) {
        utf8AppendCodepoint(base, out);
        haveBase = false;
      }
      utf8AppendCodepoint(cp, out);
    } else {
      // Hangul LV / LVT composition (Unicode 3.12, pure arithmetic — no
      // tables): a modern leading consonant followed by a medial vowel
      // composes to an LV syllable in the U+AC00 block; an LV syllable
      // followed by a trailing consonant extends to LVT. macOS stores
      // filenames in NFD, so Korean names arrive as conjoining jamo, which
      // the fonts (precomposed syllables only) would miss entirely.
      if (haveBase) {
        if (base >= 0x1100 && base <= 0x1112 && cp >= 0x1161 && cp <= 0x1175) {
          base = 0xAC00 + (base - 0x1100) * 588 + (cp - 0x1161) * 28;
          continue;
        }
        if (base >= 0xAC00 && base <= 0xD7A3 && (base - 0xAC00) % 28 == 0 && cp >= 0x11A8 && cp <= 0x11C2) {
          base += cp - 0x11A7;
          continue;
        }
        utf8AppendCodepoint(base, out);
      }
      base = cp;
      haveBase = true;
    }
  }
  if (haveBase) utf8AppendCodepoint(base, out);
  return out;
}

int utf8CodepointLen(const unsigned char c) {
  if (c < 0x80) return 1;          // 0xxxxxxx
  if ((c >> 5) == 0x6) return 2;   // 110xxxxx
  if ((c >> 4) == 0xE) return 3;   // 1110xxxx
  if ((c >> 3) == 0x1E) return 4;  // 11110xxx
  return 1;                        // fallback for invalid
}

uint32_t utf8NextCodepoint(const unsigned char** string) {
  if (**string == 0) {
    return 0;
  }

  const unsigned char lead = **string;
  const int bytes = utf8CodepointLen(lead);
  const uint8_t* chr = *string;

  // Invalid lead byte (stray continuation byte 0x80-0xBF, or 0xFE/0xFF)
  if (bytes == 1 && lead >= 0x80) {
    (*string)++;
    return REPLACEMENT_GLYPH;
  }

  if (bytes == 1) {
    (*string)++;
    return chr[0];
  }

  // Validate continuation bytes before consuming them
  for (int i = 1; i < bytes; i++) {
    if ((chr[i] & 0xC0) != 0x80) {
      // Missing or invalid continuation byte — skip all bytes consumed so far
      *string += i;
      return REPLACEMENT_GLYPH;
    }
  }

  uint32_t cp = chr[0] & ((1 << (7 - bytes)) - 1);  // mask header bits

  for (int i = 1; i < bytes; i++) {
    cp = (cp << 6) | (chr[i] & 0x3F);
  }

  // Reject overlong encodings, surrogates, and out-of-range values
  const bool overlong = (bytes == 2 && cp < 0x80) || (bytes == 3 && cp < 0x800) || (bytes == 4 && cp < 0x10000);
  const bool surrogate = (cp >= 0xD800 && cp <= 0xDFFF);
  if (overlong || surrogate || cp > 0x10FFFF) {
    (*string)++;
    return REPLACEMENT_GLYPH;
  }

  *string += bytes;

  return cp;
}

char* utf8EncodeCodepoint(const uint32_t cp, char* out) {
  if (cp < 0x80) {
    *out++ = static_cast<char>(cp);
  } else if (cp < 0x800) {
    *out++ = static_cast<char>(0xC0 | (cp >> 6));
    *out++ = static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    *out++ = static_cast<char>(0xE0 | (cp >> 12));
    *out++ = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    *out++ = static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    *out++ = static_cast<char>(0xF0 | (cp >> 18));
    *out++ = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    *out++ = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    *out++ = static_cast<char>(0x80 | (cp & 0x3F));
  }
  return out;
}

void utf8AppendCodepoint(const uint32_t cp, std::string& out) {
  char bytes[4];
  out.append(bytes, utf8EncodeCodepoint(cp, bytes));
}

int utf8SafeTruncateBuffer(const char* buf, int len) {
  if (len <= 0) return 0;

  // Walk back past continuation bytes (10xxxxxx) to find the lead byte
  int leadPos = len - 1;
  while (leadPos > 0 && (static_cast<uint8_t>(buf[leadPos]) & 0xC0) == 0x80) {
    leadPos--;
  }

  // Determine expected length of the sequence starting at leadPos
  int expectedLen = utf8CodepointLen(static_cast<unsigned char>(buf[leadPos]));
  int actualLen = len - leadPos;

  if (actualLen < expectedLen && leadPos > 0) {
    // Incomplete UTF-8 sequence at the end — exclude it
    return leadPos;
  }
  return len;
}

size_t utf8RemoveLastChar(std::string& str) {
  if (str.empty()) return 0;
  size_t pos = str.size() - 1;
  while (pos > 0 && (static_cast<unsigned char>(str[pos]) & 0xC0) == 0x80) {
    --pos;
  }
  str.resize(pos);
  return pos;
}

// Truncate string by removing N UTF-8 characters from the end
void utf8TruncateChars(std::string& str, const size_t numChars) {
  for (size_t i = 0; i < numChars && !str.empty(); ++i) {
    utf8RemoveLastChar(str);
  }
}

TextEncodingGuess utf8GuessEncoding(const unsigned char* data, const size_t len) {
  bool sawMultiByte = false;
  size_t i = 0;
  while (i < len) {
    const unsigned char b = data[i];
    if (b < 0x80) {
      ++i;
      continue;
    }
    size_t need;
    uint32_t minCp;
    if ((b & 0xE0) == 0xC0) {
      need = 1;
      minCp = 0x80;
    } else if ((b & 0xF0) == 0xE0) {
      need = 2;
      minCp = 0x800;
    } else if ((b & 0xF8) == 0xF0) {
      need = 3;
      minCp = 0x10000;
    } else {
      return TextEncodingGuess::Legacy8Bit;  // a stray continuation byte or 0xF8-0xFF
    }
    if (i + need >= len) break;  // cut off by the end of the sample
    uint32_t cp = b & (0x3F >> need);
    for (size_t k = 1; k <= need; ++k) {
      const unsigned char c = data[i + k];
      if ((c & 0xC0) != 0x80) return TextEncodingGuess::Legacy8Bit;
      cp = (cp << 6) | (c & 0x3F);
    }
    if (cp < minCp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return TextEncodingGuess::Legacy8Bit;
    sawMultiByte = true;
    i += need + 1;
  }
  return sawMultiByte ? TextEncodingGuess::Utf8 : TextEncodingGuess::Undecided;
}

std::string cp1252ToUtf8(const char* data, const size_t len) {
  // 0x80-0x9F; 0 marks the five bytes Windows-1252 leaves unassigned.
  static constexpr uint16_t kHigh[32] = {0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                         0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
                                         0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                         0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};
  std::string out;
  out.reserve(len + len / 16);
  for (size_t i = 0; i < len; ++i) {
    const auto b = static_cast<unsigned char>(data[i]);
    if (b < 0x80) {
      out.push_back(static_cast<char>(b));
    } else if (b < 0xA0) {
      utf8AppendCodepoint(kHigh[b - 0x80] ? kHigh[b - 0x80] : '?', out);
    } else {
      utf8AppendCodepoint(b, out);
    }
  }
  return out;
}
