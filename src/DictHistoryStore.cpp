#include "DictHistoryStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include "util/RecentWordList.h"

void DictHistoryStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["words"].to<JsonArray>();
  // c_str(), never the std::string: ArduinoJson's std::string converter drags a per-TU copy
  // of the serializer into flash and copies every word into the document's pool. The words
  // outlive the write, so linking the pointer is safe. See the note in PersistableStore.h.
  for (const auto& word : words) arr.add(word.c_str());
}

bool DictHistoryStore::fromJson(JsonVariantConst doc) {
  words.clear();
  JsonArrayConst arr = doc["words"].as<JsonArrayConst>();
  words.reserve(std::min(arr.size(), MAX_WORDS));
  for (JsonVariantConst entry : arr) {
    if (words.size() >= MAX_WORDS) break;
    const char* raw = entry.is<const char*>() ? entry.as<const char*>() : nullptr;
    if (raw && raw[0] != '\0') words.emplace_back(raw);
  }
  LOG_DBG("DHIS", "Dictionary history loaded (%u words)", static_cast<unsigned>(words.size()));
  return true;
}

void DictHistoryStore::ensureLoaded() {
  if (loaded) return;
  // A missing file is a normal empty history and needs no retry, so only a file that exists
  // and could not be read leaves this unlatched: retrying beats overwriting a history that
  // is there but was unreadable this once.
  const bool read = loadFromFile();
  loaded = read || !Storage.exists(getFilePath());
}

void DictHistoryStore::add(const std::string& word) {
  ensureLoaded();
  // The list rule itself lives in recent_words, where it is host-tested; false means
  // nothing moved, so the write is skipped.
  if (!recent_words::moveToFront(words, word, MAX_WORDS)) return;

  if (!saveToFile()) LOG_ERR("DHIS", "Failed to save dictionary history");
}

void DictHistoryStore::clear() {
  ensureLoaded();
  if (words.empty()) return;
  words.clear();
  if (!saveToFile()) LOG_ERR("DHIS", "Failed to clear dictionary history");
}
