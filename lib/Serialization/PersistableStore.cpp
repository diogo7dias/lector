#include "PersistableStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <cstring>
#include <limits>
#include <string>

// Suffix of the staging file a save writes before it swaps. Also what loadDocFromFile()
// looks for when the real file is missing, so a save interrupted by a power cut leaves a
// recoverable file rather than nothing.
static constexpr const char* kTmpSuffix = ".tmp";

static std::string tmpPathFor(const char* path) { return std::string(path) + kTmpSuffix; }

bool PersistableStoreBase::writeDocToFile(const char* path, const JsonDocument& doc) {
  Storage.mkdir("/.crosspoint");

  // Serialized straight into the file, not through an intermediate String.
  //
  // The String was the whole problem. Arduino's String reports an allocation failure by
  // being empty, and SDCardManager::writeFile compares bytes written against content
  // length, which is trivially true when both are zero. So a serialize that ran out of
  // heap was reported as a successful save — and because writeFile removes the
  // destination before it opens it, what was left on the card was a zero-byte file with
  // the previous settings already deleted. An X4 ended up exactly there: settings.json
  // and koreader.json both zero bytes, and no setting would stick again.
  //
  // A settings document is a few hundred bytes but the String has to hold all of it in
  // one contiguous allocation, and it is asked for at the worst moment — right after a
  // chapter build, when the heap is at its most fragmented. HalFile is a Print, so
  // ArduinoJson can write through it in small pieces and the spike disappears.
  const size_t expected = measureJson(doc);
  if (expected == 0) {
    LOG_ERR("PERSIST", "Refusing to write %s: nothing to serialize", path);
    return false;
  }

  // Stage, then swap. The old file stays intact until the new content is on the card, so
  // a failure anywhere below leaves the previous save in place instead of destroying it.
  const std::string tmp = tmpPathFor(path);
  size_t written = 0;
  {
    HalFile file;
    if (!Storage.openFileForWrite("PERSIST", tmp.c_str(), file)) {
      LOG_ERR("PERSIST", "Failed to open %s for write", tmp.c_str());
      return false;
    }
    written = serializeJson(doc, file);
    file.close();
  }
  if (written != expected) {
    LOG_ERR("PERSIST", "Refusing to swap %s: wrote %u of %u bytes", path, static_cast<unsigned>(written),
            static_cast<unsigned>(expected));
    Storage.remove(tmp.c_str());
    return false;
  }

  // SdFat never overwrites on rename, so the destination has to go first. The window
  // between the two is the one place a power cut can still leave no file at the real
  // path — and the staging file survives it, which is what readDocFromFile falls back to.
  Storage.remove(path);
  if (!Storage.rename(tmp.c_str(), path)) {
    LOG_ERR("PERSIST", "Failed to swap %s into place", path);
    return false;
  }
  return true;
}

bool PersistableStoreBase::readDocFromFile(const char* path, JsonDocument& doc) {
  const char* source = path;
  std::string tmp;
  if (!Storage.exists(path)) {
    // A save that was cut short between removing the old file and renaming the staged one
    // in leaves only the staging file. Its contents are complete — it was fully written
    // and closed before the swap began — so recovering it loses nothing, and the next
    // save puts it back at the real path.
    tmp = tmpPathFor(path);
    if (!Storage.exists(tmp.c_str())) {
      return false;  // Expected on first boot — not an error.
    }
    LOG_ERR("PERSIST", "Recovering %s from an interrupted save", path);
    source = tmp.c_str();
  }
  path = source;
  String json = Storage.readFile(path);
  if (json.isEmpty() && tmp.empty()) {
    // A zero-byte file is what the old save path left behind when it ran out of heap, so
    // there may be a staged copy alongside it that is intact. Worth one look before
    // falling back to defaults and losing every setting the user ever changed.
    tmp = tmpPathFor(path);
    if (Storage.exists(tmp.c_str())) {
      LOG_ERR("PERSIST", "%s is empty; recovering from an interrupted save", path);
      json = Storage.readFile(tmp.c_str());
    }
  }
  if (json.isEmpty()) {
    LOG_ERR("PERSIST", "Failed to read %s (empty)", path);
    return false;
  }
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("PERSIST", "JSON parse error in %s: %s", path, error.c_str());
    return false;
  }
  return true;
}

std::string PersistableStoreBase::extractPassword(JsonVariantConst doc, bool& needsResave) {
  bool valid = false;
  return extractPassword(doc, needsResave, std::numeric_limits<size_t>::max(), valid);
}

std::string PersistableStoreBase::extractPassword(JsonVariantConst doc, bool& needsResave, const size_t maxLength,
                                                  bool& valid) {
  valid = true;
  bool ok = false;
  bool tooLong = false;
  std::string pass = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", maxLength, &ok, &tooLong);
  if (tooLong) {
    valid = false;
    return "";
  }
  if (!ok) {
    // Deobfuscation failed — fall back to legacy plaintext password.
    const char* legacyPassword = doc["password"] | "";
    const size_t legacyLength = strlen(legacyPassword);
    if (legacyLength > maxLength) {
      valid = false;
      return "";
    }
    pass.assign(legacyPassword, legacyLength);
    if (!pass.empty()) needsResave = true;
  }
  // A successfully decoded empty string is a legitimate value; preserve as-is.
  return pass;
}
