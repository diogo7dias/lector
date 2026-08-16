#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

/**
 * The rules that decide whether an offered file is allowed onto the card, and
 * under what name.
 *
 * Everything a sender puts in an offer is attacker-controlled in the ordinary
 * sense: it arrives over the air from a device this reader has never met, and
 * nothing about ESP-NOW proves who sent it. The name is therefore treated as
 * untrusted text rather than as a path, and the size is checked before a single
 * byte is written.
 *
 * No Arduino types, no Storage, no radio, so all of it is exercised by host
 * tests.
 */
namespace nearby_file {

/**
 * Longest filename kept from an offer. FAT long names allow 255 characters, and
 * the rest of the path plus a collision suffix has to fit alongside it.
 */
constexpr size_t MAX_FILENAME_BYTES = 180;

/**
 * Free space kept back rather than filled. A FAT card with no room left is where
 * progress files and section caches start failing to write, which reads to a
 * user as the book losing their place.
 */
constexpr uint64_t FREE_SPACE_MARGIN_BYTES = 1024ULL * 1024ULL;

/** Ceiling on a single transfer, well above any book and below any mistake. */
constexpr uint64_t MAX_TRANSFER_BYTES = 512ULL * 1024ULL * 1024ULL;

/** How many "name (N).ext" variants to try before giving up on a folder. */
constexpr int MAX_COLLISION_ATTEMPTS = 99;

enum class RejectReason : uint8_t {
  NONE,
  UNSUPPORTED_TYPE,
  TOO_LARGE,
};

struct OfferCheck {
  bool accepted = false;
  std::string safeName;
  RejectReason rejection = RejectReason::NONE;
};

/**
 * True when the name ends in a format this reader can actually open: books
 * (.epub, .txt, .md, .xtc, .xtch) and sleep or cover images (.pxc, .png, .bmp).
 * Anything else is refused by name, before any bytes are accepted.
 */
bool isAcceptedFilename(std::string_view name);

/**
 * Reduces an offered name to a bare, safe filename.
 *
 * Drops every directory component, so an offer of "../../.crosspoint/x.json"
 * cannot escape the destination folder; removes control characters, NULs, and
 * the characters FAT rejects; and caps the length while keeping the extension
 * intact, since the extension is what the accept check and the reader both key
 * off. Returns an empty string when nothing usable is left.
 */
std::string sanitizeFilename(std::string_view name);

/** True when a file of `sizeBytes` fits in `freeBytes` with the margin intact. */
bool fitsOnCard(uint64_t sizeBytes, uint64_t freeBytes);

/**
 * The path an incoming file should be written to inside `folder`, avoiding any
 * name already there by counting up: "Book.epub", "Book (2).epub", and so on. A
 * transfer never overwrites a file that is already on the card.
 *
 * `folder` is the card root when empty. Returns an empty string when every
 * candidate up to MAX_COLLISION_ATTEMPTS is taken.
 *
 * `candidate` builds the Nth name to try; the caller passes the firmware's own
 * naming helper, so a received duplicate is named exactly as one filed by any
 * other path, and this library stays free of app headers.
 */
using CandidateNamer = std::function<std::string(std::string_view name, std::string_view folder, int index)>;

std::string resolveDestination(std::string_view folder, const std::string& safeName,
                               const std::function<bool(const std::string&)>& exists, const CandidateNamer& candidate);

/** Runs the whole offer through the checks above in one call. */
OfferCheck checkOffer(std::string_view offeredName, uint64_t sizeBytes, uint64_t freeBytes);

}  // namespace nearby_file
