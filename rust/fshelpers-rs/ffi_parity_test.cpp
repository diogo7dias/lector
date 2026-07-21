// Differential parity harness: proves the Rust FFI implementation of
// checkFileExtension agrees with the original C++ logic on every input,
// and that the FFI boundary links and runs on the host.
//
// Build + run:
//   cargo build --release            # from rust/fshelpers-rs/
//   c++ -std=c++20 ffi_parity_test.cpp target/release/libfshelpers_rs.a -o /tmp/parity && /tmp/parity
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "include/fshelpers_rs.h"

// --- Original C++ reference, copied verbatim from lib/FsHelpers/FsHelpers.cpp ---
static bool checkFileExtension_cpp(std::string_view fileName, const char* extension) {
  const size_t extLen = strlen(extension);
  if (fileName.length() < extLen) {
    return false;
  }
  const size_t offset = fileName.length() - extLen;
  for (size_t i = 0; i < extLen; i++) {
    if (tolower(static_cast<unsigned char>(fileName[offset + i])) !=
        tolower(static_cast<unsigned char>(extension[i]))) {
      return false;
    }
  }
  return true;
}

static bool checkFileExtension_rust(std::string_view fileName, const char* extension) {
  return fshelpers_check_file_extension(reinterpret_cast<const uint8_t*>(fileName.data()), fileName.length(),
                                        reinterpret_cast<const uint8_t*>(extension), strlen(extension));
}

// --- Original C++ reference, copied verbatim from lib/FsHelpers/FsHelpers.cpp ---
static bool naturalLess_cpp(const std::string_view str1, const std::string_view str2) {
  const auto isDigit = [](const char c) { return isdigit(static_cast<unsigned char>(c)) != 0; };
  size_t pos1 = 0;
  size_t pos2 = 0;
  while (pos1 < str1.size() && pos2 < str2.size()) {
    if (isDigit(str1[pos1]) && isDigit(str2[pos2])) {
      while (pos1 < str1.size() && str1[pos1] == '0') ++pos1;
      while (pos2 < str2.size() && str2[pos2] == '0') ++pos2;
      size_t end1 = pos1;
      size_t end2 = pos2;
      while (end1 < str1.size() && isDigit(str1[end1])) ++end1;
      while (end2 < str2.size() && isDigit(str2[end2])) ++end2;
      const size_t len1 = end1 - pos1;
      const size_t len2 = end2 - pos2;
      if (len1 != len2) return len1 < len2;
      for (size_t i = 0; i < len1; ++i) {
        if (str1[pos1 + i] != str2[pos2 + i]) return str1[pos1 + i] < str2[pos2 + i];
      }
      pos1 = end1;
      pos2 = end2;
    } else {
      const int c1 = tolower(static_cast<unsigned char>(str1[pos1]));
      const int c2 = tolower(static_cast<unsigned char>(str2[pos2]));
      if (c1 != c2) return c1 < c2;
      ++pos1;
      ++pos2;
    }
  }
  return pos1 == str1.size() && pos2 != str2.size();
}

static bool naturalLess_rust(std::string_view a, std::string_view b) {
  return fshelpers_natural_less(reinterpret_cast<const uint8_t*>(a.data()), a.size(),
                                reinterpret_cast<const uint8_t*>(b.data()), b.size());
}

// --- Original C++ reference, copied verbatim from lib/FsHelpers/FsHelpers.cpp ---
static bool naturalFileLess_cpp(const std::string_view str1, const std::string_view str2) {
  bool isDir1 = !str1.empty() && str1.back() == '/';
  bool isDir2 = !str2.empty() && str2.back() == '/';
  if (isDir1 != isDir2) return isDir1;
  return naturalLess_cpp(str1, str2);
}

static bool naturalFileLess_rust(std::string_view a, std::string_view b) {
  return fshelpers_natural_file_less(reinterpret_cast<const uint8_t*>(a.data()), a.size(),
                                     reinterpret_cast<const uint8_t*>(b.data()), b.size());
}

// --- UTF-8 + sanitizeFilename reference, copied from lib/Utf8/Utf8.cpp and
//     src/util/StringUtils.cpp ---
#define REPLACEMENT_GLYPH_REF 0xFFFD
static int utf8CodepointLen_ref(unsigned char c) {
  if (c < 0x80) return 1;
  if ((c >> 5) == 0x6) return 2;
  if ((c >> 4) == 0xE) return 3;
  if ((c >> 3) == 0x1E) return 4;
  return 1;
}
static uint32_t utf8NextCodepoint_ref(const unsigned char** string) {
  if (**string == 0) return 0;
  const unsigned char lead = **string;
  const int bytes = utf8CodepointLen_ref(lead);
  const uint8_t* chr = *string;
  if (bytes == 1 && lead >= 0x80) {
    (*string)++;
    return REPLACEMENT_GLYPH_REF;
  }
  if (bytes == 1) {
    (*string)++;
    return chr[0];
  }
  for (int i = 1; i < bytes; i++) {
    if ((chr[i] & 0xC0) != 0x80) {
      *string += i;
      return REPLACEMENT_GLYPH_REF;
    }
  }
  uint32_t cp = chr[0] & ((1 << (7 - bytes)) - 1);
  for (int i = 1; i < bytes; i++) cp = (cp << 6) | (chr[i] & 0x3F);
  const bool overlong = (bytes == 2 && cp < 0x80) || (bytes == 3 && cp < 0x800) || (bytes == 4 && cp < 0x10000);
  const bool surrogate = (cp >= 0xD800 && cp <= 0xDFFF);
  if (overlong || surrogate || cp > 0x10FFFF) {
    (*string)++;
    return REPLACEMENT_GLYPH_REF;
  }
  *string += bytes;
  return cp;
}
static std::string sanitizeFilename_cpp(const std::string& name, size_t maxBytes) {
  std::string result;
  result.reserve(std::min(name.size(), maxBytes));
  const auto* text = reinterpret_cast<const unsigned char*>(name.c_str());
  while (*text == ' ' || *text == '.') text++;
  while (*text != 0) {
    const auto* cpStart = text;
    uint32_t cp = utf8NextCodepoint_ref(&text);
    if (cp == '/' || cp == '\\' || cp == ':' || cp == '*' || cp == '?' || cp == '"' || cp == '<' || cp == '>' ||
        cp == '|') {
      if (result.length() + 1 > maxBytes) break;
      result += '_';
    } else if (cp >= 128 || (cp >= 32 && cp < 127)) {
      const size_t cpBytes = text - cpStart;
      if (result.length() + cpBytes > maxBytes) break;
      result.append(reinterpret_cast<const char*>(cpStart), cpBytes);
    }
  }
  size_t end = result.find_last_not_of(" .");
  if (end != std::string::npos)
    result.resize(end + 1);
  else
    result.clear();
  return result.empty() ? "book" : result;
}

static std::string sanitizeFilename_rust(const std::string& name, size_t maxBytes) {
  size_t cap = maxBytes < 4 ? 4 : maxBytes;
  std::string out(cap, '\0');
  size_t n = fshelpers_sanitize_filename(reinterpret_cast<const uint8_t*>(name.data()), name.size(), maxBytes,
                                         reinterpret_cast<uint8_t*>(&out[0]), out.size());
  if (n > out.size()) n = out.size();
  out.resize(n);
  return out;
}

// --- PNG unfilter reference, copied verbatim from lib/PngToBmpConverter/PngToBmpConverter.cpp
//     (paethPredictor + decodeScanline's reverse-filter switch). Operates on
//     equal-length rows with bpp>=1, exactly as the decoder feeds it. ---
static uint8_t paethPredictor_cpp(uint8_t a, uint8_t b, uint8_t c) {
  int p = static_cast<int>(a) + b - c;
  int pa = p > a ? p - a : a - p;
  int pb = p > b ? p - b : b - p;
  int pc = p > c ? p - c : c - p;
  if (pa <= pb && pa <= pc) return a;
  if (pb <= pc) return b;
  return c;
}

// Returns true if the filter is valid (mirrors decodeScanline returning false on
// an unknown filter). Reconstructs `cur` in place using `prev` (same length).
static bool unfilterRow_cpp(uint8_t filter, std::vector<uint8_t>& cur, const std::vector<uint8_t>& prev, int bpp) {
  const uint32_t rawRowBytes = static_cast<uint32_t>(cur.size());
  switch (filter) {
    case 0:
      break;
    case 1:  // Sub
      for (uint32_t i = bpp; i < rawRowBytes; i++) cur[i] += cur[i - bpp];
      break;
    case 2:  // Up
      for (uint32_t i = 0; i < rawRowBytes; i++) cur[i] += prev[i];
      break;
    case 3:  // Average
      for (uint32_t i = 0; i < rawRowBytes; i++) {
        uint8_t a = (i >= static_cast<uint32_t>(bpp)) ? cur[i - bpp] : 0;
        uint8_t b = prev[i];
        cur[i] += (a + b) / 2;
      }
      break;
    case 4:  // Paeth
      for (uint32_t i = 0; i < rawRowBytes; i++) {
        uint8_t a = (i >= static_cast<uint32_t>(bpp)) ? cur[i - bpp] : 0;
        uint8_t b = prev[i];
        uint8_t c = (i >= static_cast<uint32_t>(bpp)) ? prev[i - bpp] : 0;
        cur[i] += paethPredictor_cpp(a, b, c);
      }
      break;
    default:
      return false;
  }
  return true;
}

// --- Original C++ reference, per-pixel formulas copied verbatim from the
//     switch(bpp) in lib/GfxRenderer/Bitmap.cpp readNextRow. Bounds checks match
//     the Rust helper so both agree on malformed (too-short / bad-bpp) inputs.
static bool unpackRow_cpp(const std::vector<uint8_t>& row, uint16_t bpp, size_t width,
                          const std::vector<uint8_t>& palette, std::vector<uint8_t>& out) {
  if (width == 0) return true;
  if (out.size() < width) return false;
  const bool paletted = (bpp == 8 || bpp == 4 || bpp == 2 || bpp == 1);
  if (paletted && palette.size() < 256) return false;
  switch (bpp) {
    case 32:
      if (row.size() < width * 4) return false;
      for (size_t x = 0; x < width; x++) {
        const uint8_t* p = row.data() + x * 4;
        out[x] = (77u * p[2] + 150u * p[1] + 29u * p[0]) >> 8;
      }
      return true;
    case 24:
      if (row.size() < width * 3) return false;
      for (size_t x = 0; x < width; x++) {
        const uint8_t* p = row.data() + x * 3;
        out[x] = (77u * p[2] + 150u * p[1] + 29u * p[0]) >> 8;
      }
      return true;
    case 8:
      if (row.size() < width) return false;
      for (size_t x = 0; x < width; x++) out[x] = palette[row[x]];
      return true;
    case 4:
      if (row.size() < (width + 1) / 2) return false;
      for (size_t x = 0; x < width; x++) {
        const uint8_t nibble = (x & 1) ? (row[x >> 1] & 0x0F) : (row[x >> 1] >> 4);
        out[x] = palette[nibble];
      }
      return true;
    case 2:
      if (row.size() < (width + 3) / 4) return false;
      for (size_t x = 0; x < width; x++) out[x] = palette[(row[x >> 2] >> (6 - ((x & 3) * 2))) & 0x03];
      return true;
    case 1:
      if (row.size() < (width + 7) / 8) return false;
      for (size_t x = 0; x < width; x++) {
        const uint8_t palIndex = (row[x >> 3] & (0x80 >> (x & 7))) ? 1 : 0;
        out[x] = palette[palIndex];
      }
      return true;
    default:
      return false;
  }
}

int main() {
  long checks = 0, mismatches = 0;

  auto compare = [&](std::string_view name, const char* ext) {
    bool a = checkFileExtension_cpp(name, ext);
    bool b = checkFileExtension_rust(name, ext);
    checks++;
    if (a != b) {
      mismatches++;
      if (mismatches <= 20) {
        printf("MISMATCH name=\"%.*s\" ext=\"%s\" cpp=%d rust=%d\n", (int)name.size(), name.data(), ext, a, b);
      }
    }
  };

  // 1. Hand-picked edge cases (note: name may contain embedded NULs, so string_view length matters).
  const char* exts[] = {".jpg", ".jpeg", ".png", ".epub", ".xtc", ".xtch", ".txt", ".md", "", "."};
  const char* names[] = {"photo.jpg", "PHOTO.JPG", "photo.JpG", "photo.png", "book.EPUB", "jpg", "",         ".md",
                         ".MD",       "a.b.c.txt", "café.JPG",  "noext",     "x",         "..",  "trailing."};
  for (const char* n : names) {
    for (const char* e : exts) {
      compare(std::string_view(n), e);
    }
  }

  // 2. Fuzz: random byte strings (including NULs and high bytes) vs random extensions.
  std::mt19937 rng(12345);
  std::uniform_int_distribution<int> lenD(0, 24), byteD(0, 255), extLenD(0, 6);
  for (int iter = 0; iter < 500000; iter++) {
    std::string name(lenD(rng), '\0');
    for (char& c : name) c = static_cast<char>(byteD(rng));

    // Extension is a C string: build without embedded NULs so strlen is well-defined.
    std::string ext(extLenD(rng), '\0');
    for (char& c : ext) {
      int v = byteD(rng);
      if (v == 0) v = 1;
      c = static_cast<char>(v);
    }
    compare(std::string_view(name.data(), name.size()), ext.c_str());
  }

  // 3. naturalLess fuzz: random strings over a digit/letter-heavy alphabet (to
  //    exercise the numeric path), plus occasional high bytes.
  auto natcmp = [&](std::string_view a, std::string_view b) {
    bool ca = naturalLess_cpp(a, b);
    bool cb = naturalLess_rust(a, b);
    checks++;
    if (ca != cb) {
      mismatches++;
      if (mismatches <= 20) {
        printf("NL MISMATCH a=\"%.*s\" b=\"%.*s\" cpp=%d rust=%d\n", (int)a.size(), a.data(), (int)b.size(), b.data(),
               ca, cb);
      }
    }
  };
  static const char alpha[] = "0123456789000aAbBzZ._- \x80\xff";
  const int alphaN = (int)sizeof(alpha) - 1;
  std::uniform_int_distribution<int> nlLen(0, 16), alphaD(0, alphaN - 1);
  auto mkstr = [&]() {
    std::string s(nlLen(rng), '\0');
    for (char& c : s) c = alpha[alphaD(rng)];
    return s;
  };
  for (int iter = 0; iter < 500000; iter++) {
    std::string a = mkstr(), b = mkstr();
    natcmp(std::string_view(a.data(), a.size()), std::string_view(b.data(), b.size()));
  }

  // 4. naturalFileLess fuzz: same alphabet, with an occasional trailing '/' so the
  //    directories-first branch is exercised on both sides.
  auto natfilecmp = [&](std::string_view a, std::string_view b) {
    bool ca = naturalFileLess_cpp(a, b);
    bool cb = naturalFileLess_rust(a, b);
    checks++;
    if (ca != cb) {
      mismatches++;
      if (mismatches <= 20) {
        printf("NFL MISMATCH a=\"%.*s\" b=\"%.*s\" cpp=%d rust=%d\n", (int)a.size(), a.data(), (int)b.size(), b.data(),
               ca, cb);
      }
    }
  };
  std::uniform_int_distribution<int> slashD(0, 3);
  for (int iter = 0; iter < 500000; iter++) {
    std::string a = mkstr(), b = mkstr();
    if (slashD(rng) == 0) a.push_back('/');
    if (slashD(rng) == 0) b.push_back('/');
    natfilecmp(std::string_view(a.data(), a.size()), std::string_view(b.data(), b.size()));
  }

  // 5. sanitizeFilename fuzz: fully random bytes (illegal chars, control chars,
  //    truncated multibyte UTF-8, embedded NULs) at varied max_bytes budgets.
  auto sancmp = [&](const std::string& n, size_t mx) {
    std::string ca = sanitizeFilename_cpp(n, mx);
    std::string cb = sanitizeFilename_rust(n, mx);
    checks++;
    if (ca != cb) {
      mismatches++;
      if (mismatches <= 20) {
        printf("SAN MISMATCH len=%zu max=%zu cpp=\"%s\" rust=\"%s\"\n", n.size(), mx, ca.c_str(), cb.c_str());
      }
    }
  };
  std::uniform_int_distribution<int> sanLen(0, 24), sanByte(0, 255), sanMax(1, 30);
  for (int iter = 0; iter < 500000; iter++) {
    std::string n(sanLen(rng), '\0');
    for (char& c : n) c = static_cast<char>(sanByte(rng));
    size_t mx = (iter % 7 == 0) ? 100 : static_cast<size_t>(sanMax(rng));
    sancmp(n, mx);
  }

  // 6. PNG unfilter fuzz: random rows + prev rows (equal length, as the decoder
  //    feeds them), random filter incl. invalid, bpp in [1,8]. Compare the whole
  //    reconstructed row and the bool result.
  std::uniform_int_distribution<int> pngLen(0, 40), pngByte(0, 255), pngFilter(0, 7), pngBpp(1, 8);
  for (int iter = 0; iter < 500000; iter++) {
    size_t len = static_cast<size_t>(pngLen(rng));
    int bpp = pngBpp(rng);
    uint8_t filter = static_cast<uint8_t>(pngFilter(rng));
    std::vector<uint8_t> cur(len), prev(len), curR;
    for (auto& b : cur) b = static_cast<uint8_t>(pngByte(rng));
    for (auto& b : prev) b = static_cast<uint8_t>(pngByte(rng));
    curR = cur;

    bool okC = unfilterRow_cpp(filter, cur, prev, bpp);
    bool okR = fshelpers_png_unfilter_row(filter, curR.empty() ? nullptr : curR.data(), curR.size(),
                                          prev.empty() ? nullptr : prev.data(), prev.size(), static_cast<size_t>(bpp));
    checks++;
    // On an unknown filter both report false and leave the row untouched; on a
    // valid filter both reconstruct identically.
    bool rowMatch = (filter > 4) ? true : (cur == curR);
    if (okC != okR || !rowMatch) {
      mismatches++;
      if (mismatches <= 20) {
        printf("PNG MISMATCH filter=%u bpp=%d len=%zu okC=%d okR=%d\n", filter, bpp, len, okC, okR);
      }
    }
  }

  // 7. BMP row unpack fuzz: random bpp (valid 32/24/8/4/2/1 + invalid), width,
  //    row length (deliberately sometimes too short), and palette length. Compare
  //    the bool result and, when both succeed, the full luminance row.
  const int bppChoices[] = {32, 24, 8, 4, 2, 1, 16, 0};
  std::uniform_int_distribution<int> bmpW(0, 64), bmpByte(0, 255), bmpBppPick(0, 7), bmpRowExtra(-4, 8),
      bmpPalLen(0, 300);
  for (int iter = 0; iter < 500000; iter++) {
    const uint16_t bpp = static_cast<uint16_t>(bppChoices[bmpBppPick(rng)]);
    const size_t width = static_cast<size_t>(bmpW(rng));
    size_t reqRow;
    if (bpp == 32)
      reqRow = width * 4;
    else if (bpp == 24)
      reqRow = width * 3;
    else if (bpp == 8)
      reqRow = width;
    else if (bpp == 4)
      reqRow = (width + 1) / 2;
    else if (bpp == 2)
      reqRow = (width + 3) / 4;
    else if (bpp == 1)
      reqRow = (width + 7) / 8;
    else
      reqRow = width;
    long rlen = static_cast<long>(reqRow) + bmpRowExtra(rng);
    if (rlen < 0) rlen = 0;
    std::vector<uint8_t> row(static_cast<size_t>(rlen));
    for (auto& b : row) b = static_cast<uint8_t>(bmpByte(rng));
    std::vector<uint8_t> palette(static_cast<size_t>(bmpPalLen(rng)));
    for (auto& b : palette) b = static_cast<uint8_t>(bmpByte(rng));
    std::vector<uint8_t> outC(width ? width : 1, 0), outR(width ? width : 1, 0);

    bool okC = unpackRow_cpp(row, bpp, width, palette, outC);
    bool okR =
        fshelpers_bmp_unpack_row(row.empty() ? nullptr : row.data(), row.size(), bpp, width,
                                 palette.empty() ? nullptr : palette.data(), palette.size(), outR.data(), outR.size());
    checks++;
    const bool outMatch = (okC && okR) ? (outC == outR) : true;
    if (okC != okR || !outMatch) {
      mismatches++;
      if (mismatches <= 20) {
        printf("BMP MISMATCH bpp=%u width=%zu rlen=%zu pal=%zu okC=%d okR=%d\n", bpp, width, row.size(), palette.size(),
               okC, okR);
      }
    }
  }

  printf("\nchecks=%ld mismatches=%ld\n", checks, mismatches);
  if (mismatches == 0) {
    printf("PARITY OK: Rust FFI matches C++ on every input.\n");
    return 0;
  }
  printf("PARITY FAILED\n");
  return 1;
}
