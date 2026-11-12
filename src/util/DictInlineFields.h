#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Slack required above a definition buffer before we commit to it, so a request that would only
// just fit is refused rather than left to abort mid-read.
constexpr uint32_t DEFINITION_HEAP_HEADROOM_BYTES = 8 * 1024;

enum class InlineDecode : uint8_t {
  Ok,
  Empty,      // no text field: only binary data, or nothing at all
  Damaged,    // the fields do not parse
  LowMemory,  // the decoded text would not fit on the heap
};

// A StarDict .ifo without sametypesequence tags each entry's fields with their type: a lowercase
// type byte and NUL-terminated text, or an uppercase type byte and a big-endian size before
// binary data. Rewrites text from byte `start` as HTML: markup fields (h, x) as they are, other
// text escaped in <pre>, binary fields dropped. `truncated` accepts a last field cut short by
// the definition size cap.
InlineDecode decodeInlineFields(std::string& text, size_t start, bool truncated);

size_t escapedHtmlSize(std::string_view text);
void appendEscapedHtml(std::string& out, std::string_view text);
