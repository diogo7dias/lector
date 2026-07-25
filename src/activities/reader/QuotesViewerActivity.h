#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Reads back the quotes Grab Quote saved: the "<book>_QUOTES.txt" sidecar written
// by QuoteSelectActivity, in the same "[chapter]\nquote\n---\n\n" format (see
// QuoteText.h). Up/Down scroll, hold Confirm to delete the selected quote (behind
// a ConfirmationActivity), Back leaves.
//
// The whole sidecar is parsed into RAM on entry. That is bounded by the writer's
// own cap (quote_text::MAX_QUOTES_FILE_BYTES, 24KB) and refused up front when the
// largest free heap block cannot take it, so it never grows without a ceiling.
class QuotesViewerActivity final : public Activity {
 public:
  struct QuoteEntry {
    std::string chapter;
    std::string text;
  };

  QuotesViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& quotesFilePath)
      : Activity("QuotesViewer", renderer, mappedInput), filePath(quotesFilePath) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void loadQuotes();
  bool saveQuotes() const;
  void confirmDeleteSelected();
  void deleteSelected();
  static std::string deriveBookTitle(const std::string& path);

  std::string filePath;
  std::string bookTitle;
  std::vector<QuoteEntry> quotes;

  int selectorIndex = 0;
  // Rows wrap to as many lines as the quote needs, so the visible window comes back
  // from the draw (see BaseTheme::drawWrappedList) and feeds the next scroll offset.
  int scrollOffset = 0;
  int firstVisibleIdx = 0;
  int lastVisibleIdx = 0;

  // A Confirm still held from the screen that opened this one (or from the delete
  // confirmation) must not read as a fresh hold. Cleared on the first loop that
  // sees Confirm up.
  bool confirmHoldConsumed = true;

  ButtonNavigator buttonNavigator;
};
