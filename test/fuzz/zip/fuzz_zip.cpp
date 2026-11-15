// libFuzzer harness for ZipFile. The input is the EPUB file's bytes; it is served
// through the same in-memory stub storage as test/zip_file. Each run builds fresh
// ZipFile objects, so nothing carries over between inputs except the stub's file slot,
// which setFile() replaces every run.
#include <HalStorage.h>
#include <ZipFile.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr size_t MAX_NAMES = 64;           // entries exercised per input
constexpr size_t MAX_OUTPUT = 256 * 1024;  // decompressed bytes accepted per entry

// Accepts at most MAX_OUTPUT bytes. A short write makes readFileToStream stop early
// (allowEarlyStop), so a deflate bomb costs at most MAX_OUTPUT of work.
class CappedSink : public Print {
 public:
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t*, size_t size) override {
    const size_t room = MAX_OUTPUT - total;
    const size_t take = size < room ? size : room;
    total += take;
    return take;
  }

 private:
  size_t total = 0;
};

// One entry through each lookup path: a fresh archive (scan), an open archive (cursor
// and last-lookup cache), and the entry index when the archive was large enough to index.
void exerciseEntry(ZipFile& zip, const std::string& name, size_t chunkSize) {
  size_t inflated = 0;
  zip.getInflatedFileSize(name.c_str(), &inflated);
  CappedSink sink;
  zip.readFileToStream(name.c_str(), sink, chunkSize, true);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static const std::string path = "/books/fuzz.epub";
  Storage.setFile(path, std::vector<uint8_t>(data, data + size));

  // Listing: the central directory, names copied out before any lookup.
  std::vector<std::string> names;
  ZipFile listing(path);
  listing.enumerateFilePaths([&names](std::string_view name) {
    if (names.size() < MAX_NAMES) names.emplace_back(name);
  });
  names.emplace_back("META-INF/container.xml");  // the EPUB entry point, found or not
  names.emplace_back("missing.xhtml");

  // Lookups on a fresh archive each time (scan from the start).
  for (size_t i = 0; i < names.size(); i++) {
    ZipFile zip(path);
    exerciseEntry(zip, names[i], i % 2 ? 7 : 512);
  }

  // Lookups while the archive stays open (cursor wrap and last-lookup cache).
  ZipFile open(path);
  if (open.open()) {
    for (size_t i = 0; i < names.size(); i++) exerciseEntry(open, names[i], 64);
    open.close();
  }

  // Lookups through the entry index, which only exists for archives large enough to index.
  ZipFile::EntryIndex index;
  if (ZipFile(path).buildEntryIndex(index)) {
    ZipFile indexed(path, &index);
    for (size_t i = 0; i < names.size(); i++) exerciseEntry(indexed, names[i], 512);
  }
  return 0;
}
