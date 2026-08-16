#include "NearbyFileRules.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace nearby_file {
namespace {

// Formats this reader can open: books first, then the image formats the sleep
// screen and cover viewer use.
constexpr std::array<std::string_view, 8> ACCEPTED_EXTENSIONS = {".epub", ".txt", ".md",  ".xtc",
                                                                 ".xtch", ".pxc", ".png", ".bmp"};

/** The reserved and control characters that have no business in a FAT name. */
bool isUnsafeCharacter(const unsigned char ch) {
  if (ch < 0x20 || ch == 0x7F) return true;
  switch (ch) {
    case '/':
    case '\\':
    case ':':
    case '*':
    case '?':
    case '"':
    case '<':
    case '>':
    case '|':
      return true;
    default:
      return false;
  }
}

std::string toLower(std::string_view text) {
  std::string lowered(text);
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                 [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return lowered;
}

/** The extension including its dot, lowercased. Empty when there is not one. */
std::string extensionOf(std::string_view name) {
  const size_t dot = name.rfind('.');
  // A leading dot is a hidden file, not an extension, and a trailing dot is no
  // extension at all.
  if (dot == std::string_view::npos || dot == 0 || dot + 1 >= name.size()) return {};
  return toLower(name.substr(dot));
}

}  // namespace

bool isAcceptedFilename(const std::string_view name) {
  const std::string extension = extensionOf(name);
  if (extension.empty()) return false;
  return std::find(ACCEPTED_EXTENSIONS.begin(), ACCEPTED_EXTENSIONS.end(), extension) != ACCEPTED_EXTENSIONS.end();
}

std::string sanitizeFilename(const std::string_view name) {
  // Only the final component survives, so no offered name can point anywhere but
  // the destination folder.
  std::string_view bare = name;
  const size_t lastSlash = bare.find_last_of("/\\");
  if (lastSlash != std::string_view::npos) bare = bare.substr(lastSlash + 1);

  std::string cleaned;
  cleaned.reserve(bare.size());
  for (const char ch : bare) {
    if (!isUnsafeCharacter(static_cast<unsigned char>(ch))) cleaned.push_back(ch);
  }

  // "." and ".." survive the character filter but are directory entries, not names.
  if (cleaned == "." || cleaned == "..") return {};

  if (cleaned.size() > MAX_FILENAME_BYTES) {
    // Trim the stem, never the extension: the extension is what decides whether
    // the file is accepted at all and which reader later opens it.
    const std::string extension = extensionOf(cleaned);
    if (!extension.empty() && extension.size() < MAX_FILENAME_BYTES) {
      const std::string stem = cleaned.substr(0, cleaned.size() - extension.size());
      cleaned = stem.substr(0, MAX_FILENAME_BYTES - extension.size()) + extension;
    } else {
      cleaned = cleaned.substr(0, MAX_FILENAME_BYTES);
    }
  }

  return cleaned;
}

bool fitsOnCard(const uint64_t sizeBytes, const uint64_t freeBytes) {
  if (sizeBytes == 0 || sizeBytes > MAX_TRANSFER_BYTES) return false;
  if (freeBytes < FREE_SPACE_MARGIN_BYTES) return false;
  return sizeBytes <= freeBytes - FREE_SPACE_MARGIN_BYTES;
}

std::string resolveDestination(const std::string_view folder, const std::string& safeName,
                               const std::function<bool(const std::string&)>& exists, const CandidateNamer& candidate) {
  if (safeName.empty() || !candidate || !exists) return {};

  for (int attempt = 1; attempt <= MAX_COLLISION_ATTEMPTS; attempt++) {
    const std::string path = candidate(safeName, folder, attempt);
    if (!exists(path)) return path;
  }
  return {};
}

OfferCheck checkOffer(const std::string_view offeredName, const uint64_t sizeBytes, const uint64_t freeBytes) {
  OfferCheck check;
  check.safeName = sanitizeFilename(offeredName);

  if (check.safeName.empty() || !isAcceptedFilename(check.safeName)) {
    check.rejection = RejectReason::UNSUPPORTED_TYPE;
    return check;
  }
  if (!fitsOnCard(sizeBytes, freeBytes)) {
    check.rejection = RejectReason::TOO_LARGE;
    return check;
  }

  check.accepted = true;
  return check;
}

}  // namespace nearby_file
