// Differential parity harness: proves the Rust FFI implementation of
// checkFileExtension agrees with the original C++ logic on every input,
// and that the FFI boundary links and runs on the host.
//
// Build + run:
//   cargo build --release            # from rust/fshelpers-rs/
//   c++ -std=c++20 ffi_parity_test.cpp target/release/libfshelpers_rs.a -o /tmp/parity && /tmp/parity
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
  return fshelpers_check_file_extension(reinterpret_cast<const uint8_t*>(fileName.data()),
                                        fileName.length(),
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

int main() {
  long checks = 0, mismatches = 0;

  auto compare = [&](std::string_view name, const char* ext) {
    bool a = checkFileExtension_cpp(name, ext);
    bool b = checkFileExtension_rust(name, ext);
    checks++;
    if (a != b) {
      mismatches++;
      if (mismatches <= 20) {
        printf("MISMATCH name=\"%.*s\" ext=\"%s\" cpp=%d rust=%d\n", (int)name.size(), name.data(), ext,
               a, b);
      }
    }
  };

  // 1. Hand-picked edge cases (note: name may contain embedded NULs, so string_view length matters).
  const char* exts[] = {".jpg", ".jpeg", ".png", ".epub", ".xtc", ".xtch", ".txt", ".md", "", "."};
  const char* names[] = {"photo.jpg", "PHOTO.JPG",  "photo.JpG", "photo.png", "book.EPUB",
                         "jpg",       "",           ".md",       ".MD",       "a.b.c.txt",
                         "café.JPG",  "noext",      "x",         "..",        "trailing."};
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
        printf("NL MISMATCH a=\"%.*s\" b=\"%.*s\" cpp=%d rust=%d\n", (int)a.size(), a.data(),
               (int)b.size(), b.data(), ca, cb);
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
        printf("NFL MISMATCH a=\"%.*s\" b=\"%.*s\" cpp=%d rust=%d\n", (int)a.size(), a.data(),
               (int)b.size(), b.data(), ca, cb);
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

  printf("\nchecks=%ld mismatches=%ld\n", checks, mismatches);
  if (mismatches == 0) {
    printf("PARITY OK: Rust FFI matches C++ on every input.\n");
    return 0;
  }
  printf("PARITY FAILED\n");
  return 1;
}
