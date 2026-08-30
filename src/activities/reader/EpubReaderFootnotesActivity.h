#pragma once

#include <Epub/FootnoteEntry.h>

#include <vector>

#include "activities/UiListActivity.h"

class EpubReaderFootnotesActivity final : public UiListActivity {
 public:
  explicit EpubReaderFootnotesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       const std::vector<FootnoteEntry>& footnotes)
      : UiListActivity("EpubReaderFootnotes", renderer, mappedInput), footnotes(footnotes) {}

  void onExit() override;

 protected:
  int listCount() const override { return static_cast<int>(footnotes.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleButtons() override;
  void onBackButton() override;
  ListChrome chrome() const override;

 private:
  const std::vector<FootnoteEntry>& footnotes;
  // The rows borrow their labels from the footnotes, except the unnumbered ones,
  // which fall back to a shared translated word.
  std::vector<freeink::ui::ListItem> rows;
};
