#pragma once

#include <string>
#include <string_view>

// The reading badge on a file browser row: a chip drawn before the title, in the same
// style the home screen uses for its in-progress books.
//
// Split out of FileBrowserActivity so the rule ("100 reads as a word, anything else as a
// bracketed percentage, nothing at all for a file that was never opened") can be
// host-tested without an SD card or a framebuffer.
namespace book_badge {

// `percent` is -1 for a book that was never opened (and for anything that is not a book),
// which draws no chip. `readWord` is the translated "Read", passed in so this stays free
// of I18n.
//
// The brackets are the home screen's, kept here so both surfaces read the same at a
// glance. "Read" carries no brackets: it is a word, and bracketing it made it look like
// a truncated number.
inline std::string chipLabel(const int percent, const std::string_view readWord) {
  if (percent < 0) return {};
  if (percent >= 100) return std::string(readWord);
  return "[" + std::to_string(percent) + "%]";
}

}  // namespace book_badge
