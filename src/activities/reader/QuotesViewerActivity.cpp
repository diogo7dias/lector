#include "QuotesViewerActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <memory>

#include "MappedInputManager.h"
#include "QuoteText.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long ENTER_DELETE_MODE_MS = 700;
// Headroom demanded on top of the file bytes before the whole sidecar is pulled in:
// std::string growth aborts on OOM under -fno-exceptions, so refuse up front.
constexpr size_t LOAD_HEAP_HEADROOM = 8 * 1024;
// Share of the row width the chapter tag may claim on a row's first line.
constexpr int CHAPTER_TAG_WIDTH_DIVISOR = 4;
}  // namespace

// ── Parsing ─────────────────────────────────────────────────────────────────

std::string QuotesViewerActivity::deriveBookTitle(const std::string& path) {
  const auto slash = path.rfind('/');
  std::string filename = (slash != std::string::npos) ? path.substr(slash + 1) : path;
  static constexpr char SUFFIX[] = "_QUOTES.txt";
  constexpr size_t SUFFIX_LEN = sizeof(SUFFIX) - 1;
  if (filename.size() > SUFFIX_LEN && filename.compare(filename.size() - SUFFIX_LEN, SUFFIX_LEN, SUFFIX) == 0) {
    filename.resize(filename.size() - SUFFIX_LEN);
  }
  return filename;
}

void QuotesViewerActivity::loadQuotes() {
  quotes.clear();

  // Primary first, then ".bak". The writer only leaves a .bak behind if it died
  // between rotating the old file away and promoting the new one, so this is the
  // crash-window recovery, not a normal path.
  std::string buf;
  const std::string sources[] = {filePath, filePath + ".bak"};
  for (const auto& src : sources) {
    if (!Storage.exists(src.c_str())) continue;
    HalFile file;
    if (!Storage.openFileForRead("QV", src, file)) continue;
    const size_t fileSize = file.size();
    if (fileSize == 0) continue;
    if (fileSize > quote_text::MAX_QUOTES_FILE_BYTES) {
      LOG_ERR("QV", "Quotes file over cap: %u > %u", static_cast<unsigned>(fileSize),
              static_cast<unsigned>(quote_text::MAX_QUOTES_FILE_BYTES));
      continue;
    }
    if (ESP.getMaxAllocHeap() < fileSize + LOAD_HEAP_HEADROOM) {
      LOG_ERR("QV", "Low heap for %u byte quotes file", static_cast<unsigned>(fileSize));
      continue;
    }
    buf.assign(fileSize, '\0');
    if (file.read(&buf[0], fileSize) != static_cast<int>(fileSize)) {
      buf.clear();
      continue;
    }
    break;
  }
  if (buf.empty()) return;

  // Format written by QuoteSelectActivity / quote_text::formatQuoteEntry:
  //   [Chapter Title]\nquote text\n---\n\n
  size_t pos = 0;
  while (pos < buf.size()) {
    while (pos < buf.size() && (buf[pos] == '\n' || buf[pos] == '\r' || buf[pos] == ' ')) ++pos;
    if (pos >= buf.size()) break;

    QuoteEntry entry;
    if (buf[pos] == '[') {
      const auto close = buf.find(']', pos);
      if (close != std::string::npos) {
        entry.chapter = buf.substr(pos + 1, close - pos - 1);
        pos = close + 1;
        while (pos < buf.size() && (buf[pos] == '\n' || buf[pos] == '\r')) ++pos;
      }
    }

    const auto sep = buf.find("\n---", pos);
    if (sep == std::string::npos) {
      entry.text = buf.substr(pos);
    } else {
      entry.text = buf.substr(pos, sep - pos);
      pos = sep + 4;
    }
    while (!entry.text.empty() &&
           (entry.text.back() == '\n' || entry.text.back() == '\r' || entry.text.back() == ' ')) {
      entry.text.pop_back();
    }
    if (!entry.text.empty()) quotes.push_back(std::move(entry));
    if (sep == std::string::npos) break;
  }
}

// Rewrites the sidecar from the in-RAM list using the same tmp/backup rotation the
// writer uses: build a ".tmp", move the live file to ".bak", promote the tmp, then
// drop the backup. A failed promote puts the backup back.
bool QuotesViewerActivity::saveQuotes() const {
  const std::string tmpPath = filePath + ".tmp";
  const std::string bakPath = filePath + ".bak";

  if (quotes.empty()) {
    Storage.remove(filePath.c_str());
    Storage.remove(bakPath.c_str());
    LOG_INF("QV", "All quotes deleted, removed %s", filePath.c_str());
    return true;
  }

  Storage.remove(tmpPath.c_str());  // clear any stale temp
  {
    HalFile dst;
    if (!Storage.openFileForWrite("QV", tmpPath, dst)) {
      LOG_ERR("QV", "Cannot open temp quotes file for write");
      return false;
    }
    for (const auto& quote : quotes) {
      const std::string entry = quote_text::formatQuoteEntry(quote.chapter, quote.text);
      if (dst.write(entry.data(), entry.size()) != entry.size()) {
        LOG_ERR("QV", "Temp quotes write failed");
        dst.close();
        Storage.remove(tmpPath.c_str());
        return false;
      }
    }
    dst.flush();
    dst.close();
  }

  const bool primaryExists = Storage.exists(filePath.c_str());
  if (primaryExists) {
    Storage.remove(bakPath.c_str());  // clear any stale backup
    if (!Storage.rename(filePath.c_str(), bakPath.c_str())) {
      LOG_ERR("QV", "Quotes backup rename failed");
      Storage.remove(tmpPath.c_str());
      return false;
    }
  }
  if (!Storage.rename(tmpPath.c_str(), filePath.c_str())) {
    LOG_ERR("QV", "Quotes promote rename failed");
    if (Storage.exists(bakPath.c_str())) Storage.rename(bakPath.c_str(), filePath.c_str());
    Storage.remove(tmpPath.c_str());
    return false;
  }
  Storage.remove(bakPath.c_str());
  return true;
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

void QuotesViewerActivity::onEnter() {
  Activity::onEnter();
  bookTitle = deriveBookTitle(filePath);
  loadQuotes();
  selectorIndex = 0;
  scrollOffset = 0;
  LOG_DBG("QV", "Loaded %d quotes from %s", static_cast<int>(quotes.size()), filePath.c_str());
  requestUpdate();
}

void QuotesViewerActivity::onExit() { Activity::onExit(); }

// ── Delete ──────────────────────────────────────────────────────────────────

void QuotesViewerActivity::confirmDeleteSelected() {
  if (selectorIndex < 0 || selectorIndex >= static_cast<int>(quotes.size())) return;
  // ConfirmationActivity truncates the body to one line, which is enough to tell the
  // user which quote is about to go.
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, std::string(tr(STR_CONFIRM_DELETE_QUOTE)),
                                             quotes[selectorIndex].text),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) deleteSelected();
        requestUpdate();
      });
}

void QuotesViewerActivity::deleteSelected() {
  if (selectorIndex < 0 || selectorIndex >= static_cast<int>(quotes.size())) return;
  quotes.erase(quotes.begin() + selectorIndex);
  if (!saveQuotes()) {
    // The card is authoritative; leaving RAM ahead of it would show a quote as gone
    // that is still in the file.
    GUI.drawPopup(renderer, tr(STR_DELETE_FAILED));
    loadQuotes();
  }
  const int total = static_cast<int>(quotes.size());
  selectorIndex = std::clamp(selectorIndex, 0, std::max(0, total - 1));
  scrollOffset = std::clamp(scrollOffset, 0, selectorIndex);
}

// ── Input ───────────────────────────────────────────────────────────────────

void QuotesViewerActivity::loop() {
  if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) confirmHoldConsumed = false;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  const int totalItems = static_cast<int>(quotes.size());
  if (totalItems == 0) return;

  // Hold Confirm to delete the selected quote.
  if (!confirmHoldConsumed && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() > ENTER_DELETE_MODE_MS) {
    confirmHoldConsumed = true;
    confirmDeleteSelected();
    return;
  }

  // Rows have variable heights, so a fixed items-per-page is meaningless: the page
  // jump steps by however many rows the last draw actually fit on screen.
  const int pageItems = std::max(1, lastVisibleIdx - firstVisibleIdx + 1);

  // Keep the selection inside the drawn window. Moving past the bottom nudges the
  // offset by one and lets the draw settle the rest; jumping above the top snaps the
  // offset to the selection (which also covers the wrap from the first row to the last).
  auto followSelection = [this, totalItems] {
    if (selectorIndex > lastVisibleIdx) {
      scrollOffset = std::min(scrollOffset + 1, totalItems - 1);
    }
    if (selectorIndex < firstVisibleIdx) {
      scrollOffset = selectorIndex;
    }
    scrollOffset = std::clamp(scrollOffset, 0, std::max(0, totalItems - 1));
  };

  buttonNavigator.onNextPress([this, totalItems, followSelection] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, totalItems);
    followSelection();
    requestUpdate();
  });
  buttonNavigator.onPreviousPress([this, totalItems, followSelection] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, totalItems);
    followSelection();
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, totalItems, pageItems, followSelection] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
    followSelection();
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, totalItems, pageItems, followSelection] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
    followSelection();
    requestUpdate();
  });
}

// ── Render ──────────────────────────────────────────────────────────────────

void QuotesViewerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  // Header: book name plus how many quotes it holds.
  const std::string header = bookTitle + "  (" + std::to_string(quotes.size()) + ")";
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 header.c_str());

  const int helpLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int helpY = screen.y + screen.height - helpLineHeight - metrics.verticalSpacing;
  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = std::max(0, helpY - metrics.verticalSpacing - contentTop);

  if (quotes.empty()) {
    GUI.drawHelpText(renderer, Rect{screen.x, contentTop + contentHeight / 2, screen.width, helpLineHeight},
                     tr(STR_NO_QUOTES));
  } else {
    // Quotes are free-form sentences, not labels: drawList would ellipsise each one to a
    // single row and hide the part the user saved it for. drawWrappedList spills a quote
    // over as many lines as it needs and reports the visible range back for scrolling.
    // The chapter rides along as the right-aligned value on the row's first line.
    const int chapterMaxWidth = std::max(1, screen.width / CHAPTER_TAG_WIDTH_DIVISOR);
    const ListVisibility vis = GUI.drawWrappedList(
        renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, static_cast<int>(quotes.size()),
        selectorIndex, scrollOffset, [this](int index) { return quotes[index].text; },
        [this, chapterMaxWidth](int index) {
          const auto& chapter = quotes[index].chapter;
          if (chapter.empty()) return std::string();
          // UI_10 to match the font drawWrappedList measures and draws the value with.
          return renderer.truncatedText(UI_10_FONT_ID, chapter.c_str(), chapterMaxWidth, EpdFontFamily::REGULAR);
        });
    firstVisibleIdx = vis.firstVisible;
    lastVisibleIdx = vis.lastVisible;
    scrollOffset = vis.firstVisible;

    GUI.drawHelpText(renderer, Rect{screen.x, helpY, screen.width, helpLineHeight}, tr(STR_HOLD_TO_DELETE));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
