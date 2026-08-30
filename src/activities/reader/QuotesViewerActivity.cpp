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

namespace fui = freeink::ui;

namespace {
constexpr unsigned long ENTER_DELETE_MODE_MS = 700;
// Headroom demanded on top of the file bytes before the whole sidecar is pulled in:
// std::string growth aborts on OOM under -fno-exceptions, so refuse up front.
constexpr size_t LOAD_HEAP_HEADROOM = 8 * 1024;
// Share of the row width the chapter tag may claim on a row's first line.
constexpr int CHAPTER_TAG_WIDTH_DIVISOR = 4;
// Lines a quote may spill over before it is cut. Long enough for a paragraph,
// short enough that one quote cannot fill the screen on its own.
constexpr uint8_t MAX_QUOTE_LINES = 8;
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
    // isRecordGap covers the page-break byte each entry now starts with, so the
    // header bracket is still what the scan lands on.
    while (pos < buf.size() && quote_text::isRecordGap(buf[pos])) ++pos;
    if (pos >= buf.size()) break;

    QuoteEntry entry;
    if (buf[pos] == '[') {
      const auto close = buf.find(']', pos);
      if (close != std::string::npos) {
        // The header field carries the chapter title and, for quotes saved with
        // a position, the anchor token. Split them so the title displays clean
        // and the anchor survives a rewrite of this file.
        quote_text::splitChapterAnchor(buf.substr(pos + 1, close - pos - 1), entry.chapter, entry.anchor);
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
      const std::string entry = quote_text::formatQuoteEntry(quote.chapter, quote.anchor, quote.text);
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
  UiListActivity::onEnter();
  bookTitle = deriveBookTitle(filePath);
  loadQuotes();
  LOG_DBG("QV", "Loaded %d quotes from %s", static_cast<int>(quotes.size()), filePath.c_str());
  requestUpdate();
}

void QuotesViewerActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
  chapterTags.clear();
}

// ── Delete ──────────────────────────────────────────────────────────────────

void QuotesViewerActivity::confirmDelete(const int index) {
  if (index < 0 || index >= listCount()) return;
  // ConfirmationActivity truncates the body to one line, which is enough to tell the
  // user which quote is about to go.
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, std::string(tr(STR_CONFIRM_DELETE_QUOTE)),
                                             quotes[index].text),
      [this, index](const ActivityResult& result) {
        if (!result.isCancelled) deleteQuote(index);
        requestUpdate();
      });
}

void QuotesViewerActivity::deleteQuote(const int index) {
  if (index < 0 || index >= listCount()) return;
  {
    // The published rows borrow strings from `quotes` and `chapterTags`; erasing
    // under the lock keeps the render task from reading a freed one.
    RenderLock lock(*this);
    quotes.erase(quotes.begin() + index);
    rows.clear();
    chapterTags.clear();
    closeRouting();
  }
  if (!saveQuotes()) {
    // The card is authoritative; leaving RAM ahead of it would show a quote as gone
    // that is still in the file.
    GUI.drawPopup(renderer, tr(STR_DELETE_FAILED));
    RenderLock lock(*this);
    loadQuotes();
    rows.clear();
    chapterTags.clear();
    closeRouting();
  }
  moveSelectionTo(std::clamp(nav.selected, 0, std::max(0, listCount() - 1)));
}

// ── Input ───────────────────────────────────────────────────────────────────

bool QuotesViewerActivity::handleButtons() {
  if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) confirmHoldConsumed = false;

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBackButton();
    return true;
  }

  // Hold Confirm to delete the selected quote. A Confirm still held from the
  // screen that opened this one must not read as a fresh hold.
  if (!confirmHoldConsumed && listCount() > 0 && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() > ENTER_DELETE_MODE_MS) {
    confirmHoldConsumed = true;
    confirmDelete(nav.selected);
    return true;
  }
  // No plain Confirm action: a quote is read where it sits, so the only thing
  // the middle button does here is the hold above.
  return false;
}

void QuotesViewerActivity::onRowLongPress(const int index) { confirmDelete(index); }

void QuotesViewerActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

// ── Screen ──────────────────────────────────────────────────────────────────

ListChrome QuotesViewerActivity::chrome() const {
  // Header: book name plus how many quotes it holds.
  headerText = bookTitle + "  (" + std::to_string(quotes.size()) + ")";
  ListChrome chrome;
  chrome.title = headerText.c_str();
  // Nothing to open: the middle button only deletes, and only on a hold.
  chrome.confirmHint = "";
  if (!quotes.empty()) chrome.footnote = tr(STR_HOLD_TO_DELETE);
  return chrome;
}

void QuotesViewerActivity::buildScreen(UiScreen& screen) {
  const int count = listCount();
  if (count == 0) {
    screen.centeredText(tr(STR_NO_QUOTES));
    return;
  }

  const int chapterMaxWidth = std::max(1, renderer.getScreenWidth() / CHAPTER_TAG_WIDTH_DIVISOR);
  chapterTags.assign(static_cast<size_t>(count), std::string());
  rows.assign(static_cast<size_t>(count), fui::ListItem{});
  for (int i = 0; i < count; ++i) {
    rows[i].label = quotes[i].text.c_str();
    if (!quotes[i].chapter.empty()) {
      chapterTags[i] = renderer.truncatedText(UI_10_FONT_ID, quotes[i].chapter.c_str(), chapterMaxWidth,
                                              EpdFontFamily::REGULAR);
      rows[i].value = chapterTags[i].c_str();
    }
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  // Quotes are free-form sentences, not labels: a single-line row would ellipsise
  // each one and hide the part it was saved for. The SDK's list grows a row by the
  // lines its label actually uses, and reports the layout back so the viewport
  // follows a selection whose row is taller than the estimate.
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = MAX_QUOTE_LINES;
  syncListViewport(screen, props);
  screen.list(props);
}
