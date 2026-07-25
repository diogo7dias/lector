#include "ReaderPresetNames.h"

#include <algorithm>
#include <cctype>

namespace readerpreset {
namespace {

bool isBlank(const char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

std::string lower(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (const char c : s) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool nameTaken(const std::string& candidate, const std::vector<std::string>& existing, const int skipIndex) {
  const std::string needle = lower(candidate);
  for (size_t i = 0; i < existing.size(); i++) {
    if (static_cast<int>(i) == skipIndex) continue;
    if (lower(existing[i]) == needle) return true;
  }
  return false;
}

}  // namespace

std::string sanitizeName(const std::string& raw) {
  size_t first = 0;
  while (first < raw.size() && isBlank(raw[first])) first++;
  size_t last = raw.size();
  while (last > first && isBlank(raw[last - 1])) last--;
  std::string out = raw.substr(first, last - first);
  if (out.size() > MAX_NAME_LENGTH) out.resize(MAX_NAME_LENGTH);
  return out;
}

std::string makeUniqueName(const std::string& desired, const std::vector<std::string>& existing, const int skipIndex) {
  const std::string base = sanitizeName(desired);
  if (base.empty()) return base;
  if (!nameTaken(base, existing, skipIndex)) return base;

  for (int n = 2; n < 100; n++) {
    const std::string suffix = " " + std::to_string(n);
    // Trim the BASE to make room, never the suffix: a clipped suffix would collide
    // with the name it was meant to distinguish itself from.
    std::string trimmed = base;
    if (trimmed.size() + suffix.size() > MAX_NAME_LENGTH) {
      trimmed.resize(MAX_NAME_LENGTH - suffix.size());
    }
    const std::string candidate = trimmed + suffix;
    if (!nameTaken(candidate, existing, skipIndex)) return candidate;
  }
  return base;  // 98 same-named presets is a user problem, not a reason to spin
}

}  // namespace readerpreset
