#pragma once

#include <Epub.h>
#include <Epub/Page.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"

// Grab Quote: pick a passage on the current reader page with the buttons and
// save it to "<book>_QUOTES.txt". Two phases:
//   SelectStart  Left/Right step words, Up/Down jump rows, Confirm sets the
//                first word, Back returns to the reader.
//   SelectEnd    the cursor extends the selection forward (>= start); a
//                continuous bar highlights the range. Confirm saves + returns;
//                Back drops back to SelectStart.
// Single page only (v1): a quote must lie within one page. Cross-page selection
// is a future extension. The old fork's in-reader "highlight mode" is not used —
// this is a standalone activity (like DictionaryWordSelectActivity) so it never
// collides with the reader's grayscale/paperback render path.
class QuoteSelectActivity final : public Activity {
 public:
  QuoteSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Page> page,
                      int marginLeft, int marginTop, std::shared_ptr<Epub> epub, int spineIndex, int fontId);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Screen box of one page token. `text` points into the owned Page's TextBlock
  // arena (NUL-terminated), valid for this activity's lifetime. Unlike the
  // dictionary picker, EVERY token is kept (incl. punctuation) so the joined
  // quote reproduces the passage faithfully.
  struct WordBox {
    int16_t x;
    int16_t y;
    int16_t width;
    uint16_t row;
    const char* text;
    EpdFontFamily::Style style;
  };

  enum class Phase : uint8_t { SelectStart, SelectEnd };

  void extractWords();
  int closestInRow(uint16_t row, int centerX) const;
  void moveVertical(int direction);
  void saveSelectedQuote();
  bool saveQuoteToFile(const std::string& quote);
  std::string chapterTitle() const;
  void drawRangeHighlight() const;
  void drawHints() const;

  std::unique_ptr<Page> page;
  const int marginLeft;
  const int marginTop;
  std::shared_ptr<Epub> epub;
  const int spineIndex;
  const int fontId;
  int lineHeight = 0;

  std::vector<WordBox> words;
  uint16_t rowCount = 0;
  int cursor = 0;
  int startWord = -1;
  Phase phase = Phase::SelectStart;

  // Entered while Confirm is still held (menu Confirm-release): ignore the stale
  // release until a fresh press is seen.
  bool confirmPressSeen = false;
};
