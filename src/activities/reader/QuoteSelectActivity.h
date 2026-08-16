#pragma once

#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/Section.h>

#include <memory>
#include <string>
#include <vector>

#include "QuoteText.h"
#include "activities/Activity.h"

// Grab Quote: pick a passage on the current reader page with the buttons and
// save it to "<book>_QUOTES.txt". Two phases:
//   SelectStart  Left/Right step words, Up/Down jump rows, Confirm sets the
//                first word, Back returns to the reader.
//   SelectEnd    the cursor extends the selection forward (>= start); a
//                continuous bar highlights the range. Confirm saves + returns;
//                Back drops back to SelectStart.
// A quote may run past the end of the page: extending the cursor off the last
// word turns to the next page and keeps going, and pulling it back off the first
// word returns. Only one page is ever held in RAM — the words already passed are
// folded into a plain string (committedText) as each page is left behind. The
// quote stays inside one chapter: page turning stops at the chapter's last page.
// The old fork's in-reader "highlight mode" is not used — this is a standalone
// activity (like DictionaryWordSelectActivity) so it never collides with the
// reader's grayscale/paperback render path.
class QuoteSelectActivity final : public Activity {
 public:
  QuoteSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Section* section, int startPageNumber,
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
    // Chapter-local paragraph this word belongs to (0 = not derivable from this
    // page); saved with the quote so the reader can find it again later.
    uint16_t paragraphOrdinal;
    const char* text;
    EpdFontFamily::Style style;
  };

  enum class Phase : uint8_t { SelectStart, SelectEnd };

  void extractWords();
  int closestInRow(uint16_t row, int centerX) const;
  bool mayLeavePage() const;
  bool moveCursorTo(int index);
  void moveVertical(int direction);
  // Index of the first selected word on the page being shown: the start word on the
  // page the quote began on, and word 0 on every page it has since run into.
  int firstSelectedOnPage() const;
  // Replaces the shown page. Leaves the current page in place and returns false when
  // the load fails, so a bad read never empties the screen mid-selection.
  bool showPage(int number);
  // Extend past the last word / pull back before the first, one page at a time.
  bool advancePage();
  bool retreatPage();
  void saveSelectedQuote();
  bool saveQuoteToFile(const std::string& quote, const std::string& anchorToken);
  std::string chapterTitle() const;
  void drawRangeHighlight() const;
  void drawHints() const;

  // Owned by the reader, which is suspended for this activity's whole lifetime.
  Section* section;
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

  // Page currently shown, and the page the quote started on. They differ once the
  // selection has run past a page end.
  int pageNumber;
  int startPageNumber;
  // Words of the pages already passed, joined with the same rule as the final quote.
  // Only this string survives a page turn; the words themselves are dropped with the page.
  std::string committedText;
  // committedText's length before each page was folded in, so pulling the selection
  // back a page restores the text exactly.
  std::vector<uint32_t> committedMarks;
  // Captured when the start word is confirmed, while its page is still loaded.
  quote_text::QuoteAnchor startAnchor;
};
