#pragma once

#include <cstddef>
#include <string>
#include <vector>

/**
 * The WiFi networks and OPDS servers of one reader, in a form that can be handed
 * to another over the Nearby radio.
 *
 * Setting a second reader up means typing the same WiFi password and the same
 * OPDS login on a five-button keyboard all over again. This carries them across
 * instead.
 *
 * The bundle holds passwords in the clear. It is written to the card only for as
 * long as the transfer takes and deleted at both ends, but ESP-NOW is not
 * encrypted and anyone in radio range can hear it, so both readers ask before
 * anything moves. See ShareCredentialsActivity.
 *
 * No Arduino types and no Storage here, so the format is covered by host tests.
 */
namespace credential_bundle {

/** Name the bundle travels under. The extension is what marks it for import. */
constexpr const char* FILE_EXTENSION = ".cpcred";
constexpr const char* FILE_NAME = "credentials.cpcred";

/**
 * Format version. A reader refuses a bundle it does not know how to read rather
 * than guessing at the fields.
 */
constexpr int FORMAT_VERSION = 1;

/** Bound on what one bundle may carry, so a malformed offer cannot exhaust the heap. */
constexpr size_t MAX_ENTRIES = 16;

struct WifiEntry {
  std::string ssid;
  std::string password;
};

struct OpdsEntry {
  std::string name;
  std::string url;
  std::string username;
  std::string password;
};

struct Bundle {
  std::vector<WifiEntry> wifi;
  std::vector<OpdsEntry> opds;

  bool empty() const { return wifi.empty() && opds.empty(); }
};

/** Serialises `bundle` as the JSON document that travels over the radio. */
std::string serialize(const Bundle& bundle);

/**
 * Reads a bundle back. Returns false for anything this firmware cannot trust:
 * malformed JSON, an unknown version, or an entry with no SSID or no URL.
 * Entries past MAX_ENTRIES are dropped rather than making the whole bundle fail,
 * so a reader with a longer list still shares most of it.
 */
bool parse(const std::string& json, Bundle& out);

/** True when `name` is a credential bundle by extension, case-insensitively. */
bool isBundleFilename(const std::string& name);

}  // namespace credential_bundle
