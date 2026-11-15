// libFuzzer harnesses the OPDS feed path the way the downloader drives it.
// Input layout: byte 0 picks the write() chunk size (1..512), the rest is the feed.
#include <OpdsParser.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size == 0) return 0;

  // Linear map of 0..255 onto 1..512 so every chunk size is reachable.
  const size_t chunk = 1 + (static_cast<size_t>(data[0]) * 511) / 255;
  const uint8_t* xml = data + 1;
  const size_t xmlSize = size - 1;

  // Fresh parser per input: no state carries between runs.
  OpdsParser parser;
  for (size_t off = 0; off < xmlSize; off += chunk) {
    const size_t len = std::min(chunk, xmlSize - off);
    if (len == 1) {
      parser.write(xml[off]);  // exercise the single-byte overload too
    } else {
      parser.write(xml + off, len);
    }
  }
  parser.flush();

  // Read every result the parser exposes so the accessors are reached.
  volatile size_t sink = 0;
  sink = sink + parser.error() + parser.truncated() + parser.heapAborted();
  sink = sink + parser.entryCount();
  sink = sink + parser.getSearchTemplate().size();
  sink = sink + parser.getNextPageUrl().size();
  sink = sink + parser.getPrevPageUrl().size();
  sink = sink + static_cast<size_t>(static_cast<bool>(parser));

  std::vector<OpdsEntry> entries = parser.takeEntries();
  for (const OpdsEntry& entry : entries) {
    sink = sink + static_cast<size_t>(entry.type) + entry.title.size() + entry.author.size() + entry.href.size();
    sink = sink + static_cast<size_t>(entry.title.empty()) + static_cast<size_t>(entry.href.empty());
  }
  sink = sink + parser.entryCount();  // must be zero after takeEntries()
  return 0;
}
