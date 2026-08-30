#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Reads back the quotes Grab Quote saved: the "<book>_QUOTES.txt" sidecar written
// by QuoteSelectActivity, in the same "[chapter]\nquote\n---\n\n" format (see
// QuoteText.h). Up/Down scroll, hold Confirm to delete the selected quote (behind
// a ConfirmationActivity), Back leaves.
//
// The whole sidecar is parsed into RAM on entry. That is bounded by the writer's
// own cap (quote_text::MAX_QUOTES_FILE_BYTES, 24KB) and refused up front when the
// largest free heap block cannot take it, so it never grows without a ceiling.
class QuotesViewerActivity final : public UiListActivity {
 public:
  struct QuoteEntry {
    std::string chapter;
    // Position token as stored in the header ("@q1:spine,para,hint"), empty for
    // quotes saved before anchors existed. Kept verbatim so deleting one quote
    // never strips the others' underlines.
    std::string anchor;
    std::string text;
  };

  QuotesViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& quotesFilePath)
      : UiListActivity("QuotesViewer", renderer, mappedInput, /*wantsTouchLongPress=*/true),
        filePath(quotesFilePath) {}

  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override { return static_cast<int>(quotes.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override {}
  void onRowLongPress(int index) override;
  bool handleButtons() override;
  void onBackButton() override;
  ListChrome chrome() const override;

 private:
  void loadQuotes();
  bool saveQuotes() const;
  void confirmDelete(int index);
  void deleteQuote(int index);
  static std::string deriveBookTitle(const std::string& path);

  std::string filePath;
  std::string bookTitle;
  std::vector<QuoteEntry> quotes;

  // The rows and the truncated chapter tags behind their value column; buildScreen
  // only hands out pointers, so both outlive it.
  std::vector<freeink::ui::ListItem> rows;
  std::vector<std::string> chapterTags;
  // Header text, held for the same reason.
  mutable std::string headerText;

  // A Confirm still held from the screen that opened this one (or from the delete
  // confirmation) must not read as a fresh hold. Cleared on the first loop that
  // sees Confirm up.
  bool confirmHoldConsumed = true;
};
