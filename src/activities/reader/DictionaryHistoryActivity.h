#pragma once
#include <I18n.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include <vector>

#include "activities/UiListActivity.h"
#include "util/Dictionary.h"

// The words looked up in the dictionary, newest first, with the definition one press
// away. Opened from the reader menu beside Look Up, because recall belongs where the
// lookup itself lives.
//
// The definitions are not stored: choosing a word runs the same dictionary lookup the
// page-side word select runs, and opens the same viewer with the result.
class DictionaryHistoryActivity final : public UiListActivity {
 public:
  DictionaryHistoryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("DictionaryHistory", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override { return rowCount(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override;
  // While a popup is up the screen owns the pass: it is either waiting on the
  // dictionary or counting a message out.
  bool handleCustomInput() override;
  bool drawOverlay() override;

 private:
  enum class Popup : uint8_t { None, Busy, NotFound, Error };

  size_t wordCount() const;
  // Rows are the words, then a "Clear History" row whenever there is anything to clear.
  int rowCount() const;
  bool isClearRow(size_t index) const;
  void confirmClear();
  void lookUp(const std::string& word);

  std::vector<freeink::ui::ListItem> rows;
  Popup popup = Popup::None;
  StrId popupMsg = StrId::STR_DICT_LOOKING_UP;
  unsigned long popupShownAt = 0;

  // One dictionary handle for the whole screen: opening it is the expensive part, and a
  // reader looking up several words in a row would otherwise pay it every time.
  Dictionary dict;
  bool dictOpenAttempted = false;
  bool dictOpenOk = false;
  bool dictNeedsIndex = false;
};
