#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

#include "activities/reader/ReaderPrefs.h"

// A named set of reader look settings — "Reading Themes" in the UI.
//
// Called a PRESET in code on purpose: UITheme / BaseTheme / ThemeMetrics already mean
// the menu chrome in this codebase, and a ReadingTheme sitting beside them would read
// as one of those.
struct ReaderPreset {
  std::string name;
  ReaderPrefs prefs;
  // false when the file predates the status bar in themes; see keepStatusBarWhenAbsent.
  bool carriesStatusBar = true;
};

// Presets live in one file on the card and are applied to the book the user is in.
// They do NOT carry the screen orientation: that is a device setting, so a preset touching
// it would have to write the global settings file, and the per-book model rests on a
// custom book never disturbing global state. The status bar is per book (ReaderPrefs), so
// a theme carries it like any other look field.
class ReaderPresetStore : public PersistableStore<ReaderPresetStore> {
 private:
  std::vector<ReaderPreset> presets;

  ReaderPresetStore() = default;
  ~ReaderPresetStore() = default;

  friend class PersistableStore<ReaderPresetStore>;

 public:
  // Eight fits one screen without paging, and a longer list is harder to choose from
  // rather than more useful.
  static constexpr size_t MAX_PRESETS = 8;

  static const char* getFilePath() { return "/.crosspoint/reader_presets.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  const std::vector<ReaderPreset>& getPresets() const { return presets; }
  const ReaderPreset* get(size_t index) const;
  size_t getCount() const { return presets.size(); }
  bool isFull() const { return presets.size() >= MAX_PRESETS; }

  // Every name currently in use, for the unique-name helper.
  std::vector<std::string> names() const;

  // All return false without touching the file when the index is out of range or the
  // list is full. Each persists on success.
  bool add(const std::string& name, const ReaderPrefs& prefs);
  bool rename(size_t index, const std::string& name);
  bool overwrite(size_t index, const ReaderPrefs& prefs);
  bool remove(size_t index);
};

#define READER_PRESETS ReaderPresetStore::lazyInstance()
