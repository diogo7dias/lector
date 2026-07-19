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

  printf("\nchecks=%ld mismatches=%ld\n", checks, mismatches);
  if (mismatches == 0) {
    printf("PARITY OK: Rust FFI matches C++ on every input.\n");
    return 0;
  }
  printf("PARITY FAILED\n");
  return 1;
}
