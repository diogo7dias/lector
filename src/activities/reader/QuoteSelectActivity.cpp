#include "QuoteSelectActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <climits>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "util/QuoteText.h"

QuoteSelectActivity::QuoteSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Section* section,
                                         int startPageNumber, int marginLeft, int marginTop, Epub& epub, int spineIndex,
                                         int fontId, const int holdX, const int holdY)
    : Activity("QuoteSelect", renderer, mappedInput),
      section(section),
      marginLeft(marginLeft),
      marginTop(marginTop),
      epub(epub),
      spineIndex(spineIndex),
      fontId(fontId),
      holdX(holdX),
      holdY(holdY),
      pageNumber(startPageNumber),
      startPageNumber(startPageNumber) {}

void QuoteSelectActivity::onEnter() {
  Activity::onEnter();
  showPage(pageNumber);
  cursor = 0;
  if (holdX >= 0) {
    // Opened by a hold in the reader, finger still down: select the held word and let the
    // drag that may follow move the end.
    const int hit = nearestWord(holdX, holdY, 4);
    if (hit < 0) {
      missedHold = true;
    } else {
      cursor = hit;
      setStartWord(hit);
      phase = Phase::SelectEnd;
      drag = Drag::End;
      touchSelection = true;
      heldWord = true;
    }
  }
  requestUpdate();
}

bool QuoteSelectActivity::showPage(const int number) {
  if (!section || number < 0 || number >= static_cast<int>(section->pageCount)) return false;
  auto loaded = section->loadPage(number);
  if (!loaded) return false;  // keep the page already on screen
  page = std::move(loaded);
  extractWords();
  return true;
}

int QuoteSelectActivity::firstSelectedOnPage() const {
  if (phase != Phase::SelectEnd) return 0;
  return (pageNumber == startPageNumber) ? startWord : 0;
}

// Fold every still-selected word of this page into committedText, then show the next
// page with the cursor on its first word. Refused at the chapter's last page, at the
// quote length cap, and on a failed page load — in each case nothing changes.
bool QuoteSelectActivity::advancePage() {
  if (phase != Phase::SelectEnd || !section) return false;
  if (pageNumber + 1 >= static_cast<int>(section->pageCount)) return false;

  const uint32_t mark = static_cast<uint32_t>(committedText.size());
  for (int i = firstSelectedOnPage(); i < static_cast<int>(words.size()); i++) {
    if (!quote_text::appendQuoteWord(committedText, words[i].text())) {
      committedText.resize(mark);  // at the cap: stop here rather than save a cut word
      return false;
    }
  }
  if (!showPage(pageNumber + 1)) {
    committedText.resize(mark);
    return false;
  }
  committedMarks.push_back(mark);
  pageNumber++;
  cursor = 0;
  requestUpdate();
  return true;
}

// Undo one advancePage: back to the previous page with its words selected again.
bool QuoteSelectActivity::retreatPage() {
  if (phase != Phase::SelectEnd || committedMarks.empty()) return false;
  if (!showPage(pageNumber - 1)) return false;

  committedText.resize(committedMarks.back());
  committedMarks.pop_back();
  pageNumber--;
  cursor = words.empty() ? 0 : static_cast<int>(words.size()) - 1;
  requestUpdate();
  return true;
}

void QuoteSelectActivity::extractWords() {
  words.clear();
  words.reserve(128);
  rowCount = 0;

  // Merge the page's codepoints into the SD font's advance table first so word
  // widths measure on the in-RAM path (no per-word SD glyph loads). Built-in
  // fonts skip this. Every token is kept — punctuation included — so the joined
  // quote reproduces the passage.
  std::string pageText;
  pageText.reserve(2048);
  uint8_t styleMask = 0;

  // Paragraph ordinal of the paragraph this page OPENS inside. Ordinals sit only
  // on a paragraph's first line, so a page that begins mid-paragraph carries none
  // until the next paragraph starts: the one before that first ordinal is the one
  // the page opened in. A page wholly inside one long paragraph has no ordinal at
  // all — then it stays 0, meaning "unknown", and the underline search later falls
  // back to the whole chapter.
  uint16_t runningOrdinal = 0;
  for (const auto& element : page->elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const uint16_t ordinal = static_cast<const PageLine*>(element.get())->getParagraphOrdinal();
    if (ordinal != 0) {
      runningOrdinal = static_cast<uint16_t>(ordinal - 1);
      break;
    }
  }

  for (const auto& element : page->elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const PageLine*>(element.get());
    if (line->getParagraphOrdinal() != 0) runningOrdinal = line->getParagraphOrdinal();
    const auto& block = line->getBlock();
    if (!block || !block->valid()) continue;

    bool rowHasWords = false;
    for (uint16_t i = 0; i < block->wordCount(); i++) {
      WordBox box;
      box.x = static_cast<int16_t>(line->xPos + block->wordXpos(i) + marginLeft);
      box.y = static_cast<int16_t>(line->yPos + marginTop + block->wordBoxTop(renderer, fontId, i));
      box.width = 0;   // measured below, once the advance table is ready
      box.height = 0;  // likewise, with the font resolved
      box.row = rowCount;
      box.paragraphOrdinal = runningOrdinal;
      box.index = i;
      box.block = block.get();
      words.push_back(box);
      rowHasWords = true;

      pageText.append(box.text());
      pageText.push_back(' ');
      styleMask |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(block->wordStyle(i)) & 0x03));
    }
    if (rowHasWords) rowCount++;
  }

  if (styleMask == 0) styleMask = 0x01;  // REGULAR
  // ponytail: a page with more sizes than this measures the rest off the SD card, just slower.
  int readied[4];
  size_t readiedCount = 0;
  for (auto& word : words) {
    const int font = word.block->wordRenderFont(renderer, fontId, word.index);
    if (readiedCount < 4 && std::find(readied, readied + readiedCount, font) == readied + readiedCount) {
      renderer.ensureSdCardFontReady(font, pageText.c_str(), styleMask);
      readied[readiedCount++] = font;
    }
    word.width = static_cast<int16_t>(word.block->wordWidth(renderer, fontId, word.index));
    word.height = static_cast<int16_t>(renderer.getLineHeight(font));
  }
}

int QuoteSelectActivity::closestInRow(const uint16_t row, const int centerX) const {
  int best = -1;
  int bestDistance = INT_MAX;
  for (int i = 0; i < static_cast<int>(words.size()); i++) {
    if (words[i].row != row) continue;
    const int distance = std::abs(words[i].x + words[i].width / 2 - centerX);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = i;
    }
  }
  return best;
}

// Move the cursor to an absolute word index, refusing anything before the first
// selectable word of this page. Returns false when the target is out of range.
bool QuoteSelectActivity::moveCursorTo(const int index) {
  if (index < firstSelectedOnPage() || index >= static_cast<int>(words.size())) return false;
  if (index == cursor) return true;  // already there; nothing to redraw
  cursor = index;
  requestUpdate();
  return true;
}

// Whether stepping off the edge of the page may turn it. Only while picking the end of
// the quote: the start word is chosen on the page the user opened the picker from, and
// moving around there must never carry them somewhere else.
bool QuoteSelectActivity::mayLeavePage() const { return phase == Phase::SelectEnd; }

void QuoteSelectActivity::moveVertical(const int direction) {
  if (words.empty() || cursor >= static_cast<int>(words.size())) {
    // Wordless page: there is no row to move within, only the page to leave.
    if (direction > 0) {
      advancePage();
    } else {
      retreatPage();
    }
    return;
  }

  const WordBox& current = words[cursor];
  const int centerX = current.x + current.width / 2;
  int targetRow = static_cast<int>(current.row) + direction;
  if (targetRow < 0 || targetRow >= static_cast<int>(rowCount)) {
    // Off the top or bottom of the page. Picking the END of the quote carries the
    // selection onto the neighbouring page, which is how a quote spans pages. Picking
    // the START never leaves the page: the page on screen is the page the user chose,
    // so the cursor wraps to the opposite edge instead, keeping the same column.
    if (mayLeavePage() && (direction > 0 ? advancePage() : retreatPage())) return;
    targetRow = (direction > 0) ? 0 : static_cast<int>(rowCount) - 1;
  }

  if (!moveCursorTo(closestInRow(static_cast<uint16_t>(targetRow), centerX))) {
    // The nearest word in that row sits before the start word (SelectEnd on the start
    // page): fall back to the start word rather than leaving the cursor stuck.
    moveCursorTo(firstSelectedOnPage());
  }
}

// Captured whenever the start word moves, while its page is still loaded: the selection
// may leave this page, and the paragraph ordinal lives in the words of the start page.
void QuoteSelectActivity::setStartWord(const int index) {
  startWord = index;
  startAnchor.spine = static_cast<uint16_t>(spineIndex);
  startAnchor.paragraph = words[index].paragraphOrdinal;
  startAnchor.wordHint = static_cast<uint16_t>(index);
  startAnchor.valid = true;
}

std::string QuoteSelectActivity::selectedText() const {
  // Pages already passed are in committedText; the page on screen contributes the
  // words from its first selected one up to the cursor.
  std::string quote = committedText;
  for (int i = firstSelectedOnPage(); i <= cursor && i < static_cast<int>(words.size()); i++) {
    if (!quote_text::appendQuoteWord(quote, words[i].text())) break;  // at the cap
  }
  return quote;
}

void QuoteSelectActivity::loop() {
  if (missedHold) {
    mappedInput.spendTouchContact();  // the hold's lift must not turn the reader's page
    finish();
    return;
  }
  if (showPopup) {
    if (popupShownAt != 0 && millis() - popupShownAt >= 1500) {
      showPopup = false;
      popupShownAt = 0;
      requestUpdate();
    }
    return;
  }
  if (handleTouch()) return;

  if (touchSelection && phase == Phase::SelectEnd) {
    // The hints now read Cancel / Save / Look Up; Up/Down keep moving the end.
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      saveSelectedQuote();
      finish();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (SETTINGS.dictionaryName[0] != '\0') lookUpSelection();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      moveVertical(-1);
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      moveVertical(1);
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (phase == Phase::SelectEnd) {
      // Drop the whole selection, including any pages it had run into.
      phase = Phase::SelectStart;
      committedText.clear();
      committedMarks.clear();
      if (pageNumber != startPageNumber && showPage(startPageNumber)) pageNumber = startPageNumber;
      cursor = (startWord >= 0 && startWord < static_cast<int>(words.size())) ? startWord : 0;
      startWord = -1;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }

  // A selection that has already crossed a page can be saved even from a page with no
  // words of its own (an image page), so the picker never traps the user there.
  const bool canConfirm = !words.empty() || (phase == Phase::SelectEnd && !committedText.empty());
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) && canConfirm) {
    if (phase == Phase::SelectStart) {
      setStartWord(cursor);
      phase = Phase::SelectEnd;
      requestUpdate();
    } else {
      saveSelectedQuote();
      finish();
    }
    return;
  }

  // A wordless page still passes Left/Right through, so a selection running through an
  // image page can carry on to the next one instead of stopping there.
  if (words.empty() && phase == Phase::SelectStart) return;

  // Stepping off either end of the page carries the selection onto the neighbouring
  // page instead of stopping. Only in SelectEnd: the start word is picked on one page.
  const int lo = firstSelectedOnPage();
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    if (cursor > lo) {
      cursor--;
      requestUpdate();
    } else if (!mayLeavePage() || !retreatPage()) {
      // Picking the start word, or nowhere to retreat to: wrap round to the last word
      // of this page so the cursor is never stuck at the edge.
      moveCursorTo(static_cast<int>(words.size()) - 1);
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    if (cursor + 1 < static_cast<int>(words.size())) {
      cursor++;
      requestUpdate();
    } else if (!mayLeavePage() || !advancePage()) {
      moveCursorTo(lo);
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    moveVertical(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    moveVertical(1);
  }
}

// Continuous black bar over the selected range [min(start,cursor)..max], one bar
// per screen row (words sharing a y), with the covered words redrawn white.
void QuoteSelectActivity::drawRangeHighlight() const {
  const int a = firstSelectedOnPage();
  const int b = cursor;
  if (a > b) return;

  int i = a;
  while (i <= b) {
    const uint16_t row = words[i].row;
    int minX = words[i].x;
    int maxX = words[i].x + words[i].width;
    int top = words[i].y;
    int bottom = words[i].y + words[i].height;
    int j = i;
    while (j + 1 <= b && words[j + 1].row == row) {
      j++;
      minX = std::min<int>(minX, words[j].x);
      maxX = std::max<int>(maxX, words[j].x + words[j].width);
      top = std::min<int>(top, words[j].y);
      bottom = std::max<int>(bottom, words[j].y + words[j].height);
    }
    // One pixel of ink around the words, no more: the highlight marks the passage, it
    // does not band the line.
    renderer.fillRect(minX - 1, top - 1, (maxX - minX) + 2, (bottom - top) + 2, true);
    for (int k = i; k <= j; k++) {
      words[k].block->drawWord(renderer, fontId, words[k].index, words[k].x, words[k].y, false);
    }
    i = j + 1;
  }
}

// Front-button bar. Back / Confirm / Left / Right, matching the reader idiom.
void QuoteSelectActivity::drawHints() const {
  if (touchSelection && phase == Phase::SelectEnd) {
    const char* lookUp = SETTINGS.dictionaryName[0] != '\0' ? tr(STR_LOOKUP) : "";
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_SAVE), lookUp, "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }
  if (words.empty()) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void QuoteSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();

  // Same prewarm-scan-then-render pass the reader uses, so SD-card fonts hit the
  // in-RAM glyph cache during the real draw.
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  page->render(renderer, fontId, marginLeft, marginTop);
  scope.endScanAndPrewarm();
  page->render(renderer, fontId, marginLeft, marginTop);

  if (!words.empty()) {
    if (phase == Phase::SelectStart) {
      const WordBox& w = words[cursor];
      renderer.fillRect(w.x - 1, w.y - 1, w.width + 2, w.height + 2, true);
      w.block->drawWord(renderer, fontId, w.index, w.x, w.y, false);
    } else {
      drawRangeHighlight();
    }
  }

  drawHints();
  if (showPopup) {
    GUI.drawPopup(renderer, I18N.get(popupMsg));  // refreshes the panel itself
    return;
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
