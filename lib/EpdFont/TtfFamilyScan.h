#pragma once

// Pure decision rules for SD-card TTF/OTF reader fonts. No storage, no heap,
// no platform headers, so host tests cover every rule; the firmware feeds
// directory entries in and reads the resolved face slots out.
//
// Family layout on the card mirrors the .cpfont registry: one folder per
// family under /.fonts or /fonts, the folder name is the display name, and
// up to four faces (regular / bold / italic / bold-italic) inferred from the
// file names.

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ttfscan {

// Style slot indices. Same bit meaning as EpdFontFamily::Style (BOLD=1,
// ITALIC=2) so a slot index is also the family's style argument.
constexpr uint8_t STYLE_REGULAR = 0;
constexpr uint8_t STYLE_BOLD = 1;
constexpr uint8_t STYLE_ITALIC = 2;
constexpr uint8_t STYLE_BOLD_ITALIC = 3;
constexpr uint8_t STYLE_COUNT = 4;

// A vector face renders at any size; the reader offers every integer point
// size in this range. 8 pt keeps the smallest face legible on the 150 DPI
// panel, 40 pt is roughly one third of the screen height per line.
constexpr uint8_t TTF_MIN_PT = 8;
constexpr uint8_t TTF_MAX_PT = 40;

inline uint8_t clampTtfPointSize(const uint8_t pt) {
  if (pt < TTF_MIN_PT) return TTF_MIN_PT;
  if (pt > TTF_MAX_PT) return TTF_MAX_PT;
  return pt;
}

// Point size to em pixels. 150 DPI is what fontconvert.py bakes .cpfont files
// at, so "14 pt" means the same glyph height for a TTF family as for a
// bitmap one.
constexpr int TTF_DPI = 150;
inline uint16_t ttfEmPixels(const uint8_t pt) { return static_cast<uint16_t>((pt * TTF_DPI + 36) / 72); }

// Per-face upper bound: the whole file lives in PSRAM while selected.
constexpr uint32_t TTF_MAX_FACE_BYTES = 2u * 1024u * 1024u;

// Font id shared with .cpfont families (SdCardFontManager::computeFontId): FNV-1a
// continued from the content hash over the family name and point size. The id
// is the section-cache key, so a different face file, family, or size never
// serves another one's pre-rendered pages. Never 0 (the "not found" sentinel).
inline int fontIdForFamilySize(const uint32_t contentHash, const char* familyName, const uint8_t pointSize) {
  constexpr uint32_t FNV_PRIME = 16777619u;
  uint32_t hash = contentHash;
  while (*familyName) {
    hash ^= static_cast<uint8_t>(*familyName++);
    hash *= FNV_PRIME;
  }
  hash ^= pointSize;
  hash *= FNV_PRIME;
  const int id = static_cast<int>(hash);
  return id != 0 ? id : 1;
}

inline uint32_t fnv1a(uint32_t hash, const uint8_t* data, const size_t len) {
  for (size_t i = 0; i < len; ++i) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}
constexpr uint32_t FNV1A_SEED = 2166136261u;

// --- file-name rules ---------------------------------------------------------

inline bool endsWithIgnoreCase(const char* s, const char* suffix) {
  const size_t sLen = strlen(s);
  const size_t sufLen = strlen(suffix);
  if (sLen < sufLen) return false;
  for (size_t i = 0; i < sufLen; ++i) {
    if (tolower(static_cast<unsigned char>(s[sLen - sufLen + i])) != tolower(static_cast<unsigned char>(suffix[i]))) {
      return false;
    }
  }
  return true;
}

// Accepts *.ttf / *.otf. Rejects macOS resource forks and hidden files (leading
// '.' or '_'), editor backups (trailing '~'), and everything else (.cpfont,
// .tmp, .json) so a family folder can hold both formats side by side.
inline bool isTtfFontFile(const char* name) {
  if (name == nullptr || name[0] == '\0' || name[0] == '.' || name[0] == '_') return false;
  const size_t len = strlen(name);
  if (name[len - 1] == '~') return false;
  return endsWithIgnoreCase(name, ".ttf") || endsWithIgnoreCase(name, ".otf");
}

// Word-boundary, case-insensitive token test: the token must start after a
// non-alphanumeric (or the string start) and end before one, so "SemiBold"
// never matches "bold".
inline bool hasWord(const char* hay, const char* token) {
  const size_t tLen = strlen(token);
  for (size_t i = 0; hay[i] != '\0'; ++i) {
    if (i > 0 && isalnum(static_cast<unsigned char>(hay[i - 1]))) continue;
    size_t j = 0;
    while (token[j] != '\0' && hay[i + j] != '\0' && tolower(static_cast<unsigned char>(hay[i + j])) == token[j]) {
      ++j;
    }
    if (token[j] != '\0') continue;
    const char after = hay[i + tLen];
    if (after == '\0' || !isalnum(static_cast<unsigned char>(after))) return true;
  }
  return false;
}

// Style from a file name (extension included or not; case-insensitive):
//   italic  : "italic" / "oblique" as a word, or glued to a weight word
//             ("LightItalic", "SemiBoldItalic", "BoldOblique"); "ital" as a word
//   bold    : "bold" / "semibold" / "demibold" / "extrabold" as a word
//   regular : "regular" / "normal" / "book" / "roman" / "text"
//   weights : "medium" / "black" / "heavy" -> bold; "light" / "thin" -> regular
// Word means bounded by non-alphanumerics, so "SemiBold" is never plain
// "bold" by accident and "Kobold" or "Digital" carry no token at all.
// Returns false when the name carries no style token; the family resolver
// then treats the file as a regular candidate.
inline bool endsWithWeightWord(const char* s, const size_t len) {
  static constexpr const char* kWeights[] = {"light", "thin",  "medium",  "semibold", "demibold",   "extrabold", "bold",
                                             "black", "heavy", "regular", "book",     "ultralight", "extralight"};
  for (const char* w : kWeights) {
    const size_t wl = strlen(w);
    if (len >= wl && memcmp(s + len - wl, w, wl) == 0 &&
        (len == wl || !isalnum(static_cast<unsigned char>(s[len - wl - 1])))) {
      return true;
    }
  }
  return false;
}

inline bool inferTtfStyle(const char* fileName, uint8_t& styleOut) {
  char lower[128];
  size_t n = strlen(fileName);
  if (n >= sizeof(lower)) n = sizeof(lower) - 1;
  for (size_t i = 0; i < n; ++i) lower[i] = static_cast<char>(tolower(static_cast<unsigned char>(fileName[i])));
  lower[n] = '\0';
  // Strip one extension so ".ttf" cannot read as a token and "text.otf" still
  // finds "text".
  char* dot = strrchr(lower, '.');
  if (dot != nullptr && dot != lower) *dot = '\0';
  n = strlen(lower);

  bool italic = hasWord(lower, "ital");
  for (const char* suffix : {"italic", "oblique"}) {
    const size_t sl = strlen(suffix);
    if (n < sl || memcmp(lower + n - sl, suffix, sl) != 0) continue;
    const size_t preLen = n - sl;
    // Glued to a weight word or standing alone: cut it off so the weight
    // rules below see "semibold", not "semibolditalic".
    if (preLen == 0 || !isalnum(static_cast<unsigned char>(lower[preLen - 1])) || endsWithWeightWord(lower, preLen)) {
      italic = true;
      lower[preLen] = '\0';
      n = preLen;
    }
    break;
  }
  if (!italic && hasWord(lower, "italic")) italic = true;
  if (!italic && hasWord(lower, "oblique")) italic = true;

  const bool bold = hasWord(lower, "bold") || hasWord(lower, "semibold") || hasWord(lower, "demibold") ||
                    hasWord(lower, "extrabold") || hasWord(lower, "medium") || hasWord(lower, "black") ||
                    hasWord(lower, "heavy");
  if (bold) {
    styleOut = italic ? STYLE_BOLD_ITALIC : STYLE_BOLD;
    return true;
  }
  if (italic) {
    styleOut = STYLE_ITALIC;
    return true;
  }
  if (hasWord(lower, "regular") || hasWord(lower, "normal") || hasWord(lower, "book") || hasWord(lower, "roman") ||
      hasWord(lower, "text") || hasWord(lower, "light") || hasWord(lower, "thin")) {
    styleOut = STYLE_REGULAR;
    return true;
  }
  return false;
}

inline int ciCompare(const char* a, const char* b) {
  while (*a != '\0' && *b != '\0') {
    const int ca = tolower(static_cast<unsigned char>(*a));
    const int cb = tolower(static_cast<unsigned char>(*b));
    if (ca != cb) return ca - cb;
    ++a;
    ++b;
  }
  return tolower(static_cast<unsigned char>(*a)) - tolower(static_cast<unsigned char>(*b));
}

// Resolves one family folder's files into the four style slots. Feed every
// directory entry to offer() in any order, then call finish(). Rules:
//   - a style claimed by several files: the lexicographically first name wins;
//   - a file with no style token is the regular candidate (again first name wins)
//     when no explicit regular face exists;
//   - a family with no regular face after that promotes its first present slot
//     (bold, italic, then bold-italic) so the reader always has a base face;
//     a lone "Foo-Bold.ttf" therefore reads as the family's regular face.
class TtfFamilyFaces {
 public:
  static constexpr size_t NAME_CAP = 128;

  TtfFamilyFaces() { clear(); }

  void clear() {
    for (auto& s : slot_) s[0] = '\0';
    tokenless_[0] = '\0';
    candidates_ = 0;
  }

  // Returns true when `fileName` is an accepted font file (whether or not it
  // ends up in a slot).
  bool offer(const char* fileName) {
    if (!isTtfFontFile(fileName) || strlen(fileName) >= NAME_CAP) return false;
    if (candidates_ < 255) ++candidates_;

    uint8_t style = STYLE_REGULAR;
    if (!inferTtfStyle(fileName, style)) {
      if (tokenless_[0] == '\0' || ciCompare(fileName, tokenless_) < 0) copy(tokenless_, fileName);
      return true;
    }
    if (slot_[style][0] == '\0' || ciCompare(fileName, slot_[style]) < 0) copy(slot_[style], fileName);
    return true;
  }

  void finish() {
    if (slot_[STYLE_REGULAR][0] != '\0') return;
    if (tokenless_[0] != '\0') {
      copy(slot_[STYLE_REGULAR], tokenless_);
      return;
    }
    for (uint8_t s = STYLE_BOLD; s < STYLE_COUNT; ++s) {
      if (slot_[s][0] != '\0') {
        copy(slot_[STYLE_REGULAR], slot_[s]);
        slot_[s][0] = '\0';
        return;
      }
    }
  }

  // File name for a style slot, or "" when the family has no such face.
  const char* file(const uint8_t style) const { return style < STYLE_COUNT ? slot_[style] : ""; }
  bool has(const uint8_t style) const { return style < STYLE_COUNT && slot_[style][0] != '\0'; }
  bool empty() const { return candidates_ == 0; }

 private:
  static void copy(char* dst, const char* src) {
    // offer() already rejected names at or beyond NAME_CAP.
    const size_t n = strlen(src);
    memcpy(dst, src, n + 1);
  }

  char slot_[STYLE_COUNT][NAME_CAP];
  char tokenless_[NAME_CAP];
  uint8_t candidates_;
};

// --- font file metrics ---------------------------------------------------------

// Design-unit metrics the Epd font model needs but TtfFont does not expose:
// TtfFont sizes faces by (ascender - descender) pixel height, while .cpfont
// sizes are em based (FreeType). Reading 'head' and 'hhea' directly lets a
// TTF "14 pt" match a .cpfont "14 pt".
struct TtfMetrics {
  uint16_t unitsPerEm = 0;
  int16_t ascender = 0;   // hhea, positive
  int16_t descender = 0;  // hhea, negative below the baseline
  int16_t lineGap = 0;
};

inline uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
inline uint32_t be32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

// Parses the sfnt table directory (first font of a collection). Every offset
// is bounds-checked against `len`; a truncated or foreign file returns false.
inline bool readTtfMetrics(const uint8_t* d, const size_t len, TtfMetrics& out) {
  if (d == nullptr || len < 12) return false;
  size_t base = 0;
  if (memcmp(d, "ttcf", 4) == 0) {
    // Header: tag, version, numFonts, offsets[numFonts]. First font only.
    if (len < 16) return false;
    base = be32(d + 12);
    if (base > len || len - base < 12) return false;
  }
  const uint32_t tag = be32(d + base);
  if (tag != 0x00010000u && tag != 0x74727565u /*true*/ && tag != 0x4F54544Fu /*OTTO*/) return false;
  const uint16_t numTables = be16(d + base + 4);
  if (base + 12 + static_cast<size_t>(numTables) * 16 > len) return false;

  const uint8_t* head = nullptr;
  const uint8_t* hhea = nullptr;
  for (uint16_t i = 0; i < numTables; ++i) {
    const uint8_t* rec = d + base + 12 + static_cast<size_t>(i) * 16;
    const uint32_t off = be32(rec + 8);
    const uint32_t tlen = be32(rec + 12);
    if (off > len || tlen > len - off) continue;
    if (memcmp(rec, "head", 4) == 0 && tlen >= 54) head = d + off;
    if (memcmp(rec, "hhea", 4) == 0 && tlen >= 36) hhea = d + off;
  }
  if (head == nullptr || hhea == nullptr) return false;
  out.unitsPerEm = be16(head + 18);
  out.ascender = static_cast<int16_t>(be16(hhea + 4));
  out.descender = static_cast<int16_t>(be16(hhea + 6));
  out.lineGap = static_cast<int16_t>(be16(hhea + 8));
  return out.unitsPerEm >= 16 && out.ascender > out.descender;
}

// Pixel height TtfFont must be told so that the em measures `emPx` pixels:
// TtfFont scales by pixel height over (ascender - descender).
inline uint16_t ttfHeightForEm(const TtfMetrics& m, const uint16_t emPx) {
  const int32_t extent = static_cast<int32_t>(m.ascender) - m.descender;
  const int32_t h = (static_cast<int32_t>(emPx) * extent + m.unitsPerEm / 2) / m.unitsPerEm;
  return static_cast<uint16_t>(h < 1 ? 1 : (h > 0xFFFF ? 0xFFFF : h));
}

// Round-to-nearest scaling of a design-unit value to pixels at `emPx`.
inline int ttfScaleUnits(const TtfMetrics& m, const int32_t units, const uint16_t emPx) {
  const int32_t num = units * static_cast<int32_t>(emPx);
  const int32_t half = m.unitsPerEm / 2;
  return static_cast<int>(num >= 0 ? (num + half) / m.unitsPerEm : -((-num + half) / m.unitsPerEm));
}

// --- glyph bitmap packing --------------------------------------------------------

// EpdFont 2-bit format: four pixels per byte, most significant pair first,
// rows packed back to back with no padding; 0 = white, 3 = black. Coverage is
// quantised exactly like fontconvert.py (levels at 64/128/192 of 255).
inline size_t packed2BitBytes(const int w, const int h) {
  return (static_cast<size_t>(w) * static_cast<size_t>(h) + 3) / 4;
}

inline void pack2Bit(const uint8_t* coverage, const int w, const int h, uint8_t* out) {
  const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
  const size_t bytes = packed2BitBytes(w, h);
  for (size_t b = 0; b < bytes; ++b) out[b] = 0;
  for (size_t i = 0; i < n; ++i) {
    const uint8_t level = static_cast<uint8_t>(coverage[i] >> 6);
    out[i >> 2] |= static_cast<uint8_t>(level << ((3 - (i & 3)) * 2));
  }
}

}  // namespace ttfscan
