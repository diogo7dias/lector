// libFuzzer harness for CssParser. Input layout:
//   byte 0       bit 0 set: the rest is also tried as a raw cache file (with a valid version byte)
//   bytes 1..    up to three NUL-separated parts: stylesheet, inline style, class attr, id attr
// Every run gets a fresh parser and a scratch directory whose cache files are wiped before and after.

#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include "CssParser.h"

namespace fs = std::filesystem;

namespace {

constexpr std::array<std::string_view, 8> kTags = {"p", "div", "span", "h1", "blockquote", "a", "body", "li"};

// Per-process scratch directory, removed when the fuzzer exits.
struct Scratch {
  fs::path dir;
  Scratch() : dir(fs::temp_directory_path() / ("lector_fuzz_css_" + std::to_string(getpid()))) {
    std::error_code ec;
    fs::create_directories(dir, ec);
  }
  ~Scratch() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  std::string path() const { return dir.string(); }
  std::string file(const char* name) const { return (dir / name).string(); }
};

Scratch& scratch() {
  static Scratch instance;
  return instance;
}

// Removes every file a parser run can leave behind, so runs never see each other's state.
void wipeFiles() {
  static constexpr const char* NAMES[] = {"input.css", "css_rules.cache", "css_rules.cache.tmp", "css_rules.cache.bak"};
  for (const char* name : NAMES) {
    std::error_code ec;
    fs::remove(scratch().file(name), ec);
  }
}

void writeFile(const std::string& path, const uint8_t* data, size_t size) {
  std::FILE* out = std::fopen(path.c_str(), "wb");
  if (!out) return;
  if (size) std::fwrite(data, 1, size, out);
  std::fclose(out);
}

// Fold every observable field into one value so the calls cannot be optimised away.
uint32_t digest(const CssStyle& style) {
  uint32_t acc = 0;
  acc = acc * 31 + static_cast<uint32_t>(style.textAlign);
  acc = acc * 31 + static_cast<uint32_t>(style.fontStyle);
  acc = acc * 31 + static_cast<uint32_t>(style.fontWeight);
  acc = acc * 31 + static_cast<uint32_t>(style.textDecoration);
  acc = acc * 31 + static_cast<uint32_t>(style.display);
  acc = acc * 31 + static_cast<uint32_t>(style.hasTextIndent());
  acc = acc * 31 + static_cast<uint32_t>(style.hasMarginTop());
  acc = acc * 31 + static_cast<uint32_t>(style.hasFontSize());
  acc = acc * 31 + static_cast<uint32_t>(style.hasVisibleBorder());
  return acc;
}

// Resolve a few tag/class/id/ancestor combinations; the digests feed a volatile sink.
uint32_t resolveAll(const CssParser& parser, std::string_view classAttr, std::string_view idAttr,
                    std::string_view other) {
  uint32_t acc = 0;
  CssAncestor ancestors[3];
  size_t ancestorCount = 0;
  for (size_t i = 0; i < kTags.size(); ++i) {
    const std::string_view tag = kTags[i];
    acc += digest(parser.resolveStyle(tag, classAttr, idAttr));
    acc += digest(parser.resolveStyle(tag, other, {}, nullptr, 0, true));
    if (ancestorCount < 3 && (i % 2) == 0) {
      ancestors[ancestorCount++] = CssParser::makeAncestor(tag, classAttr, idAttr);
    }
    acc += digest(parser.resolveStyle(tag, classAttr, idAttr, ancestors, ancestorCount));
  }
  return acc;
}

// Field-level comparison for the cache round trip: the same tags must resolve identically.
void requireSameStyles(const CssParser& expected, const CssParser& actual, std::string_view classAttr) {
  for (const std::string_view tag : kTags) {
    const CssStyle a = expected.resolveStyle(tag, classAttr);
    const CssStyle b = actual.resolveStyle(tag, classAttr);
    if (a.textAlign != b.textAlign || a.fontStyle != b.fontStyle || a.fontWeight != b.fontWeight ||
        a.textDecoration != b.textDecoration || a.display != b.display || a.hasTextIndent() != b.hasTextIndent() ||
        a.hasMarginTop() != b.hasMarginTop()) {
      std::fprintf(stderr, "css cache round trip changed styles for <%.*s>\n", static_cast<int>(tag.size()),
                   tag.data());
      std::abort();
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size == 0) return 0;
  const bool rawCache = (data[0] & 1) != 0;
  const uint8_t* body = data + 1;
  const size_t bodySize = size - 1;

  // Split the body on up to three NUL bytes: stylesheet, inline style, class attr, id attr.
  std::array<std::string_view, 4> parts;
  size_t count = 0;
  size_t start = 0;
  for (size_t i = 0; i < bodySize && count < 3; ++i) {
    if (body[i] == 0) {
      parts[count++] = std::string_view(reinterpret_cast<const char*>(body + start), i - start);
      start = i + 1;
    }
  }
  parts[count++] = std::string_view(reinterpret_cast<const char*>(body + start), bodySize - start);
  const std::string_view sheet = parts[0];
  const std::string_view inlineStyle = parts[1];
  const std::string_view classAttr = parts[2];
  const std::string_view idAttr = parts[3];

  volatile uint32_t sink = 0;

  // Stylesheet from a file, as the reader loads it, then every query path.
  wipeFiles();
  writeFile(scratch().file("input.css"), reinterpret_cast<const uint8_t*>(sheet.data()), sheet.size());
  {
    CssParser parser(scratch().path());
    HalFile source;
    if (HalStorage::getInstance().openFileForRead("FUZZ", scratch().file("input.css"), source)) {
      sink = sink + static_cast<uint32_t>(parser.loadFromStream(source));
    }
    sink = sink + resolveAll(parser, classAttr, idAttr, inlineStyle);
    sink = sink + static_cast<uint32_t>(parser.hasFirstLetterRules()) + static_cast<uint32_t>(parser.isPartial());
    sink = sink + static_cast<uint32_t>(parser.ruleCount());

    // Inline style="" declarations, straight from the attribute value.
    sink = sink + digest(CssParser::parseInlineStyle(inlineStyle));
    sink = sink + digest(CssParser::parseInlineStyle(sheet));

    // Cache round trip: save, inspect, reload fully and header-only, then compare.
    const bool saved = parser.saveToCache();
    sink = sink + static_cast<uint32_t>(parser.inspectCache());
    sink = sink + static_cast<uint32_t>(parser.hasCache());
    if (saved && !parser.isPartial()) {
      CssParser reader(scratch().path());
      if (reader.loadFromCache() == CssParser::CacheLoadResult::Complete) {
        requireSameStyles(parser, reader, classAttr);
      }
      CssParser headerReader(scratch().path());
      sink = sink + static_cast<uint32_t>(headerReader.loadFromCache(true));
    }
    parser.deleteCache();
  }

  // Raw cache bytes: the input as a cache file, so the loader's length and flag checks run.
  if (rawCache) {
    wipeFiles();
    std::string raw;
    raw.push_back(static_cast<char>(CssParser::CSS_CACHE_VERSION));
    raw.append(reinterpret_cast<const char*>(body), bodySize);
    writeFile(scratch().file("css_rules.cache"), reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
    CssParser parser(scratch().path());
    sink = sink + static_cast<uint32_t>(parser.loadFromCache());
    sink = sink + resolveAll(parser, classAttr, idAttr, inlineStyle);
  }

  wipeFiles();
  return 0;
}
