#include "QuoteSelectActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <GrowthBounds.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <climits>

#include "QuoteText.h"
#include "components/UITheme.h"

QuoteSelectActivity::QuoteSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         std::unique_ptr<Page> page, int marginLeft, int marginTop,
                                         std::shared_ptr<Epub> epub, int spineIndex, int fontId)
    : Activity("QuoteSelect", renderer, mappedInput),
      page(std::move(page)),
      marginLeft(marginLeft),
      marginTop(marginTop),
      epub(std::move(epub)),
      spineIndex(spineIndex),
      fontId(fontId) {}

void QuoteSelectActivity::onEnter() {
  Activity::onEnter();
  lineHeight = renderer.getLineHeight(fontId);
  extractWords();
  cursor = 0;
  requestUpdate();
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

  for (const auto& element : page->elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const PageLine*>(element.get());
    const auto& block = line->getBlock();
    if (!block || !block->valid()) continue;

    bool rowHasWords = false;
    for (uint16_t i = 0; i < block->wordCount(); i++) {
      WordBox box;
      box.x = static_cast<int16_t>(line->xPos + block->wordXpos(i) + marginLeft);
      box.y = static_cast<int16_t>(line->yPos + marginTop);
      box.style = block->wordStyle(i);
      box.width = 0;  // measured below, once the advance table is ready
      box.row = rowCount;
      box.text = block->wordText(i);
      words.push_back(box);
      rowHasWords = true;

      pageText.append(box.text);
      pageText.push_back(' ');
      styleMask |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(box.style) & 0x03));
    }
    if (rowHasWords) rowCount++;
  }

  if (styleMask == 0) styleMask = 0x01;  // REGULAR
  renderer.ensureSdCardFontReady(fontId, pageText.c_str(), styleMask);
  for (auto& word : words) {
    word.width = static_cast<int16_t>(renderer.getTextAdvanceX(fontId, word.text, word.style));
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

void QuoteSelectActivity::moveVertical(const int direction) {
  const WordBox& current = words[cursor];
  const int targetRow = static_cast<int>(current.row) + direction;
  if (targetRow < 0 || targetRow >= static_cast<int>(rowCount)) return;

  const int best = closestInRow(static_cast<uint16_t>(targetRow), current.x + current.width / 2);
  const int lo = (phase == Phase::SelectEnd) ? startWord : 0;
  if (best >= lo && best != cursor) {
    cursor = best;
    requestUpdate();
  }
}

std::string QuoteSelectActivity::chapterTitle() const {
  if (!epub) return "";
  const int tocIndex = epub->getTocIndexForSpineIndex(spineIndex);
  if (tocIndex >= 0) return epub->getTocItem(tocIndex).title;
  return "Chapter " + std::to_string(spineIndex + 1);
}

void QuoteSelectActivity::saveSelectedQuote() {
  if (startWord < 0 || words.empty()) return;
  int a = startWord;
  int b = cursor;
  if (a > b) std::swap(a, b);

  std::vector<std::string> selected;
  selected.reserve(static_cast<size_t>(b - a + 1));
  for (int i = a; i <= b && i < static_cast<int>(words.size()); i++) {
    selected.emplace_back(words[i].text);
  }
  const std::string quote = quote_text::joinQuoteWords(selected);
  if (quote.empty()) return;
  saveQuoteToFile(quote);
}

// Atomic-ish read-modify-write append of one "[chapter]\nquote\n---\n\n" entry to
// the "<book>_QUOTES.txt" sidecar: stream existing bytes + the new entry into a
// .tmp, then rotate primary -> .bak, .tmp -> primary, drop .bak (restore .bak on
// promote failure). Refused when the file would exceed MAX_QUOTES_FILE_BYTES.
bool QuoteSelectActivity::saveQuoteToFile(const std::string& quote) {
  if (!epub) return false;
  const std::string path = quote_text::quotesFilePathFor(epub->getPath());
  const std::string tmpPath = path + ".tmp";
  const std::string bakPath = path + ".bak";
  const std::string entry = quote_text::formatQuoteEntry(chapterTitle(), quote);

  size_t existingSize = 0;
  const bool primaryExists = Storage.exists(path.c_str());
  if (primaryExists) {
    HalFile probe;
    if (Storage.openFileForRead("QUOTE", path, probe)) {
      existingSize = probe.size();
    }
  }
  if (!memory::canGrowWithinLimit(existingSize, entry.size(), quote_text::MAX_QUOTES_FILE_BYTES)) {
    LOG_ERR("QUOTE", "Quotes file full: %u + %u > %u", static_cast<unsigned>(existingSize),
            static_cast<unsigned>(entry.size()), static_cast<unsigned>(quote_text::MAX_QUOTES_FILE_BYTES));
    return false;
  }

  // Build the new file content in the temp file.
  {
    HalFile out;
    if (!Storage.openFileForWrite("QUOTE", tmpPath, out)) {
      LOG_ERR("QUOTE", "Cannot open temp quotes file for write");
      return false;
    }
    if (existingSize > 0) {
      HalFile in;
      if (Storage.openFileForRead("QUOTE", path, in)) {
        uint8_t buffer[512];
        int n;
        while ((n = in.read(buffer, sizeof(buffer))) > 0) {
          if (out.write(buffer, static_cast<size_t>(n)) != static_cast<size_t>(n)) {
            LOG_ERR("QUOTE", "Temp quotes write failed (copy)");
            in.close();
            out.close();
            Storage.remove(tmpPath.c_str());
            return false;
          }
        }
        in.close();
      }
    }
    if (out.write(entry.data(), entry.size()) != entry.size()) {
      LOG_ERR("QUOTE", "Temp quotes write failed (entry)");
      out.close();
      Storage.remove(tmpPath.c_str());
      return false;
    }
    out.flush();
    out.close();
  }

  // Promote temp -> primary. Keep a backup so a failed promote can be undone.
  if (primaryExists) {
    Storage.remove(bakPath.c_str());  // clear any stale backup
    if (!Storage.rename(path.c_str(), bakPath.c_str())) {
      LOG_ERR("QUOTE", "Quotes backup rename failed");
      Storage.remove(tmpPath.c_str());
      return false;
    }
  }
  if (!Storage.rename(tmpPath.c_str(), path.c_str())) {
    LOG_ERR("QUOTE", "Quotes promote rename failed");
    if (Storage.exists(bakPath.c_str())) Storage.rename(bakPath.c_str(), path.c_str());
    Storage.remove(tmpPath.c_str());
    return false;
  }
  Storage.remove(bakPath.c_str());
  LOG_INF("QUOTE", "Saved quote (%u bytes) to %s", static_cast<unsigned>(entry.size()), path.c_str());
  return true;
}

void QuoteSelectActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) confirmPressSeen = true;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (phase == Phase::SelectEnd) {
      phase = Phase::SelectStart;
      startWord = -1;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && confirmPressSeen && !words.empty()) {
    if (phase == Phase::SelectStart) {
      startWord = cursor;
      phase = Phase::SelectEnd;
      requestUpdate();
    } else {
      saveSelectedQuote();
      finish();
    }
    return;
  }

  if (words.empty()) return;

  const int lo = (phase == Phase::SelectEnd) ? startWord : 0;
  if (mappedInput.wasPressed(MappedInputManager::Button::Left) && cursor > lo) {
    cursor--;
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right) && cursor + 1 < static_cast<int>(words.size())) {
    cursor++;
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    moveVertical(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    moveVertical(1);
  }
}

// Continuous black bar over the selected range [min(start,cursor)..max], one bar
// per screen row (words sharing a y), with the covered words redrawn white.
void QuoteSelectActivity::drawRangeHighlight() const {
  int a = startWord;
  int b = cursor;
  if (a > b) std::swap(a, b);

  int i = a;
  while (i <= b) {
    const int16_t y = words[i].y;
    int minX = words[i].x;
    int maxX = words[i].x + words[i].width;
    int j = i;
    while (j + 1 <= b && words[j + 1].y == y) {
      j++;
      minX = std::min<int>(minX, words[j].x);
      maxX = std::max<int>(maxX, words[j].x + words[j].width);
    }
    renderer.fillRect(minX - 2, y - 2, (maxX - minX) + 4, lineHeight + 4, true);
    for (int k = i; k <= j; k++) {
      renderer.drawText(fontId, words[k].x, words[k].y, words[k].text, false, words[k].style);
    }
    i = j + 1;
  }
}

// Front-button bar. Back / Confirm / Left / Right, matching the reader idiom.
void QuoteSelectActivity::drawHints() const {
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
      renderer.fillRect(w.x - 2, w.y - 2, w.width + 4, lineHeight + 4, true);
      renderer.drawText(fontId, w.x, w.y, w.text, false, w.style);
    } else {
      drawRangeHighlight();
    }
  }

  drawHints();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
