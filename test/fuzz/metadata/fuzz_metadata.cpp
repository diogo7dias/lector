// libFuzzer harness for the EPUB metadata path, in Epub::load()'s order: container.xml,
// content.opf (spine and manifest into BookMetadataCache), nav then NCX table of contents,
// buildBookBin, then load() and every read accessor. Input layout:
//   byte 0   bit 0 set: the rest is a raw book.bin for load() alone (a cache cut short or
//            corrupted on the card); bits 1-7 pick the write() chunk size
//   bytes 1.. container.xml, content.opf, toc.ncx, nav.xhtml, separated by NUL bytes
#include <HalStorage.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "Epub/BookMetadataCache.h"
#include "Epub/parsers/ContainerParser.h"
#include "Epub/parsers/ContentOpfParser.h"
#include "Epub/parsers/TocNavParser.h"
#include "Epub/parsers/TocNcxParser.h"

namespace {

const std::string kCache = "/.crosspoint/epub_1";
size_t chunk = 1024;

// Feeds a document the way Epub::readItemContentsToStream does: in chunks, stopping when the
// parser takes less than it was given.
bool stream(Print& out, std::string_view doc) {
  for (size_t off = 0; off < doc.size(); off += chunk) {
    const size_t len = std::min(chunk, doc.size() - off);
    if (out.write(reinterpret_cast<const uint8_t*>(doc.data() + off), len) != len) return false;
  }
  return true;
}

void readBack(BookMetadataCache& cache) {
  if (!cache.load()) return;
  volatile size_t sink = cache.coreMetadata.title.size() + cache.coreMetadata.description.size();
  for (int i = -1; i <= cache.getSpineCount(); i++) {
    const auto spine = cache.getSpineEntry(i);
    sink = sink + spine.href.size() + spine.tocIndex + cache.getCumulativeSize(i);
  }
  for (int i = -1; i <= cache.getTocCount(); i++) {
    const auto toc = cache.getTocEntry(i);
    sink = sink + toc.title.size() + toc.href.size() + toc.anchor.size() + toc.spineIndex;
  }
  sink = sink + cache.getSpineIndexForSize(0) + cache.getSpineIndexForSize(12345) + cache.getSpineIndexForSize(~0u);
}

bool parseNav(BookMetadataCache& cache, const std::string& navPath, std::string_view doc) {
  const std::string base = navPath.substr(0, navPath.find_last_of('/') + 1);
  HalFile pages;
  Storage.openFileForWrite("FZ", kCache + "/pages.bin.part", pages);
  TocNavParser nav(base, doc.size(), &cache, &pages);
  return nav.setup() && stream(nav, doc);
}

bool parseNcx(BookMetadataCache& cache, const std::string& base, std::string_view doc) {
  TocNcxParser ncx(base, doc.size(), &cache);
  return ncx.setup() && stream(ncx, doc);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 1) return 0;
  Storage.reset();
  chunk = 1 + (data[0] >> 1) * 8;
  std::string_view rest(reinterpret_cast<const char*>(data + 1), size - 1);

  BookMetadataCache cache(kCache);
  if (data[0] & 1) {
    Storage.put(kCache + "/book.bin", std::string(rest));
    readBack(cache);
    return 0;
  }

  std::string_view parts[4];
  for (auto& part : parts) {
    const size_t nul = rest.find('\0');
    part = rest.substr(0, nul);
    rest = nul == std::string_view::npos ? std::string_view() : rest.substr(nul + 1);
  }

  ContainerParser container(parts[0].size());
  if (!container.setup() || !stream(container, parts[0]) || container.fullPath.empty()) return 0;
  const std::string opfPath = container.fullPath;
  const std::string contentBase = opfPath.substr(0, opfPath.find_last_of('/') + 1);

  if (!cache.beginWrite() || !cache.beginContentOpfPass()) return 0;
  ContentOpfParser opf(kCache, contentBase, parts[1].size(), &cache);
  if (!opf.setup() || !stream(opf, parts[1])) return 0;
  BookMetadataCache::BookMetadata metadata;
  metadata.title = opf.title;
  metadata.author = opf.author;
  metadata.language = opf.language;
  metadata.coverItemHref = opf.coverItemHref;
  metadata.description = opf.description;
  metadata.textReferenceHref = opf.textReferenceHref;
  if (!cache.endContentOpfPass() || !cache.beginTocPass()) return 0;

  // Nav first, NCX when the nav failed or covers under half the spine (Epub::load).
  bool tocParsed = !opf.tocNavPath.empty() && parseNav(cache, opf.tocNavPath, parts[3]);
  const int navCount = cache.getTocCount();
  if ((!tocParsed || navCount * 2 < cache.getSpineCount()) && !opf.tocNcxPath.empty()) {
    cache.endTocPass();
    if (!cache.beginTocPass()) return 0;
    if (!parseNcx(cache, contentBase, parts[2]) && tocParsed) {
      cache.endTocPass();
      if (!cache.beginTocPass()) return 0;
      parseNav(cache, opf.tocNavPath, parts[3]);
    }
  }
  if (!cache.endTocPass() || !cache.endWrite()) return 0;
  if (!cache.buildBookBin("/books/fuzz.epub", metadata)) return 0;
  cache.cleanupTmpFiles();
  // FUZZ_BOOKBIN=<file> saves the built book.bin, to seed the raw-load mode.
  if (const char* out = getenv("FUZZ_BOOKBIN")) {
    if (FILE* f = fopen(out, "wb")) {
      const auto& bin = *Storage.files[kCache + "/book.bin"];
      fputc(1, f);
      fwrite(bin.data(), 1, bin.size(), f);
      fclose(f);
    }
  }

  BookMetadataCache reader(kCache);
  readBack(reader);
  return 0;
}
