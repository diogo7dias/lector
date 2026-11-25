#pragma once

#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/Section.h>
#include <I18n.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/QuoteText.h"

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
//
// Touch (QuoteSelectTouch.cpp): touching a word starts a selection there and the end
// follows the finger; touching again grabs the nearer end and drags it. Holding a word
// in the reader opens this activity mid-contact (holdX/holdY), so the same hold can be
// dragged on: lifting without a drag looks the word up, as the hold always did. Once
// touched, the hints read Cancel / Save / Look Up; Up/Down still move the end and turn
// pages. A touch drag stays on the page on screen.
class QuoteSelectActivity final : public Activity {
 public:
  QuoteSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Section* section, int startPageNumber,
                      int marginLeft, int marginTop, Epub& epub, int spineIndex, int fontId, int holdX = -1,
                      int holdY = -1);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Redraws the reader's page, so it follows the reading surface's night-mode polarity.
  bool appliesNightMode() const override { return true; }

 private:
  // Screen box of one page token. `text` points into the owned Page's TextBlock
  // arena (NUL-terminated), valid for this activity's lifetime. Unlike the
  // dictionary picker, EVERY token is kept (incl. punctuation) so the joined
  // quote reproduces the passage faithfully.
  struct WordBox {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
    uint16_t row;
    // Chapter-local paragraph this word belongs to (0 = not derivable from this
    // page); saved with the quote so the reader can find it again later.
    uint16_t paragraphOrdinal;
    uint16_t index;  // into block; both live as long as the page
    const TextBlock* block;
    const char* text() const { return block->wordText(index); }
  };

  enum class Phase : uint8_t { SelectStart, SelectEnd };
  // Which end of the selection the finger is moving.
  enum class Drag : uint8_t { None, Start, End };

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
  void setStartWord(int index);
  // The selected passage, joined as the quote is saved.
  std::string selectedText() const;
  void saveSelectedQuote();
  // Touch (QuoteSelectTouch.cpp). handleTouch() owns the pass while it returns true.
  bool handleTouch();
  int nearestWord(int x, int y, int maxGap) const;
  void dragTo(int index);
  void lookUpSelection(bool fromHold = false);
  bool saveQuoteToFile(const std::string& quote, const std::string& anchorToken, const quote_text::QuoteMeta& meta);
  void appendClipping(const std::string& quote, uint32_t savedAt) const;
  std::string chapterTitle() const;
  void drawRangeHighlight() const;
  void drawHints() const;

  // Owned by the reader, which is suspended for this activity's whole lifetime.
  Section* section;
  std::unique_ptr<Page> page;
  const int marginLeft;
  const int marginTop;
  Epub& epub;
  const int spineIndex;
  const int fontId;
  const int holdX;
  const int holdY;

  Drag drag = Drag::None;
  bool touchSelection = false;  // touched once: Cancel / Save / Look Up hints, Back leaves
  bool heldWord = false;        // opened by a hold that has not dragged: the lift looks it up
  bool missedHold = false;      // the hold landed on no word: hand the page straight back
  bool showPopup = false;
  StrId popupMsg = StrId::STR_DICT_LOOKING_UP;
  unsigned long popupShownAt = 0;  // 0 while the popup is a busy wait, not a timed message

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
