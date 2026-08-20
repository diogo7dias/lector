#pragma once

#include <string>
#include <string_view>

// The right-hand label on a file browser row: the reading badge, the file type, or both.
//
// Split out of FileBrowserActivity so the rule ("100 reads as a word, anything else as a
// percentage, no separator when either half is missing") can be host-tested without an SD
// card or a framebuffer.
namespace book_badge {

// `percent` is -1 for a book that was never opened (and for anything that is not a book).
// `readWord` is the translated "Read", passed in so this stays free of I18n.
inline std::string label(const int percent, const std::string_view extension, const std::string_view readWord) {
  if (percent < 0) return std::string(extension);

  // The badge leads, because it is what the eye is scanning the list for.
  std::string badge = percent >= 100 ? std::string(readWord) : std::to_string(percent) + "%";
  if (extension.empty()) return badge;
  badge += "  ";
  badge.append(extension);
  return badge;
}

}  // namespace book_badge
