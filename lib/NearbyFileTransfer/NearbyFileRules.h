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

/** The only folder outside the card root an offer may name, and its separator. */
constexpr std::string_view FONT_FOLDER_ROOT = ".fonts";

/** Longest font family name accepted from an offer, matching the card layout. */
constexpr size_t MAX_FAMILY_NAME_BYTES = 64;

enum class RejectReason : uint8_t {
  NONE,
  UNSUPPORTED_TYPE,
  TOO_LARGE,
};

struct OfferCheck {
  bool accepted = false;
  std::string safeName;
  /** Where the file goes, relative to the card root. Empty means the root. */
  std::string safeFolder;
  RejectReason rejection = RejectReason::NONE;
};

/**
 * True when the name ends in a format this reader can actually open: books
 * (.epub, .txt, .md, .xtc, .xtch) and sleep or cover images (.pxc, .png, .bmp).
 * Anything else is refused by name, before any bytes are accepted.
 */
bool isAcceptedFilename(std::string_view name);

/**
 * True when the name is a font face file: a bare ".cpfont" with nothing but
 * letters, digits, hyphens and underscores in front of it.
 *
 * A face is deliberately absent from isAcceptedFilename. It is not a file the
 * reader opens, and it is only ever taken as part of a family install, so it is
 * accepted through checkOffer with a font folder and nowhere else.
 */
bool isFontFaceFilename(std::string_view name);

/**
 * The folder an offer may write into, or empty when the offer may not leave the
 * card root.
 *
 * Exactly one shape is allowed: ".fonts/<Family>", one level deep, where the
 * family name holds only letters, digits, hyphens and underscores. A leading
 * slash is tolerated and dropped. Everything else, including "..", a deeper
 * path, a different root, or a name with a dot or a space in it, comes back
 * empty, so an offered folder can never point at the settings folder, a book
 * folder, or anywhere above the card root.
 */
std::string sanitizeFontFolder(std::string_view folder);

/** The family a sanitised font folder installs into. Empty when it is not one. */
std::string familyNameFromFolder(std::string_view safeFolder);

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

/**
 * The same checks for an offer that names a destination folder.
 *
 * A font face is accepted only alongside a valid ".fonts/<Family>" folder, and
 * a book or an image only without one, so neither can be used to put the other
 * kind of file where it does not belong.
 */
OfferCheck checkOffer(std::string_view offeredName, uint64_t sizeBytes, uint64_t freeBytes,
                      std::string_view offeredFolder);

}  // namespace nearby_file
