#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

// The words looked up in the dictionary, newest first.
//
// Only the headword each lookup landed on is kept, not its definition: the definition is
// large, it is already on the card inside the dictionary, and re-reading it is what
// opening a history row does anyway. That keeps the file small enough to hold a hundred
// words without competing with the reader for heap.
class DictHistoryStore : public PersistableStore<DictHistoryStore> {
 private:
  std::vector<std::string> words;
  bool loaded = false;

  DictHistoryStore() = default;
  ~DictHistoryStore() = default;

  friend class PersistableStore<DictHistoryStore>;

 public:
  // Deep enough to be a real record of a book's worth of reading, shallow enough that the
  // list stays navigable by button and the file stays a few kilobytes.
  static constexpr size_t MAX_WORDS = 100;

  static const char* getFilePath() { return "/.crosspoint/dict_history.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Reads the file on first use. Deliberately not loaded at boot: most sessions never
  // look a word up, and boot pays for every store it loads.
  void ensureLoaded();

  const std::vector<std::string>& getWords() const { return words; }
  bool empty() const { return words.empty(); }

  // Records one successful lookup at the front, moving a word already in the list rather
  // than duplicating it, and drops the oldest word once the list is full. Saves only when
  // the order actually changed, so looking the same word up twice in a row costs no write.
  void add(const std::string& word);

  // Empties the list and the file behind it.
  void clear();
};

#define DICT_HISTORY DictHistoryStore::getInstance()
