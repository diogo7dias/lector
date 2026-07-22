#include "QuotesViewerActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Serialization.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "QuoteStorageLimits.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int ENTER_DELETE_MODE_MS = 700;
constexpr int DELETE_MODE_OFF = 0;
constexpr int DELETE_MODE_DISPLAY = 1;
constexpr int DELETE_MODE_CONFIRM = 2;
constexpr int LINE_HEIGHT = 60;  // header/title band + delete-preview row height (mirrors bookmarks)
}  // namespace

// ── Parsing ─────────────────────────────────────────────────────────────────

std::string QuotesViewerActivity::deriveBookTitle(const std::string& path) {
  auto slash = path.rfind('/');
  std::string filename = (slash != std::string::npos) ? path.substr(slash + 1) : path;
  const std::string suffix = "_QUOTES.txt";
  if (filename.size() > suffix.size() &&
      filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0) {
    filename = filename.substr(0, filename.size() - suffix.size());
  }
  return filename;
}

void QuotesViewerActivity::loadQuotes() {
  quotes.clear();

  // Primary, then .bak (2-layer recovery mirrors the atomic writer).
  std::string buf;
  const std::string sources[] = {filePath, filePath + ".bak"};
  for (const auto& src : sources) {
    if (!Storage.exists(src.c_str())) continue;
    HalFile file;
    if (!Storage.openFileForRead("QV", src, file)) continue;
    const size_t fileSize = file.size();
    if (fileSize == 0 || fileSize > quote_storage::MAX_FILE_BYTES ||
        !serialization::hasStringAllocationHeadroom(fileSize * 2)) {
      LOG_ERR("QV", "Quote file too large for memory-safe load: %u bytes", static_cast<unsigned>(fileSize));
      continue;
    }
    buf.resize(fileSize);
    if (file.read(buf.data(), fileSize) != static_cast<int>(fileSize)) {
      buf.clear();
      continue;
    }
    break;
  }
  if (buf.empty()) return;

  // Format:  [Chapter Title]\nquote text\n---\n\n
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

bool QuotesViewerActivity::saveQuotes() const {
  const std::string tmpPath = filePath + ".tmp";
  const std::string bakPath = filePath + ".bak";

  if (quotes.empty()) {
    Storage.remove(filePath.c_str());
    Storage.remove(bakPath.c_str());
    LOG_INF("QV", "All quotes deleted, removed %s", filePath.c_str());
    return true;
  }

  if (Storage.exists(tmpPath.c_str())) {
    Storage.remove(tmpPath.c_str());
  }
  HalFile dst;
  if (!Storage.openFileForWrite("QV", tmpPath, dst)) {
    LOG_ERR("QV", "Failed to open quotes tmp for writing: %s", tmpPath.c_str());
    return false;
  }
  for (const auto& q : quotes) {
    std::string entry;
    if (!q.chapter.empty()) entry += "[" + q.chapter + "]\n";
    entry += q.text + "\n---\n\n";
    if (dst.write(entry.c_str(), entry.size()) != entry.size()) {
      LOG_ERR("QV", "Failed to write quote entry to tmp");
      dst.close();
      Storage.remove(tmpPath.c_str());
      return false;
    }
  }
  dst.flush();
  dst.close();

  // 2-layer rotation: drop stale .bak, primary -> .bak, tmp -> primary.
  if (Storage.exists(bakPath.c_str())) {
    Storage.remove(bakPath.c_str());
  }
  if (Storage.exists(filePath.c_str())) {
    if (!Storage.rename(filePath.c_str(), bakPath.c_str())) {
      LOG_ERR("QV", "Failed to rotate %s -> %s", filePath.c_str(), bakPath.c_str());
      Storage.remove(tmpPath.c_str());
      return false;
    }
  }
  if (!Storage.rename(tmpPath.c_str(), filePath.c_str())) {
    LOG_ERR("QV", "Failed to promote quotes tmp to %s", filePath.c_str());
    if (Storage.exists(bakPath.c_str())) Storage.rename(bakPath.c_str(), filePath.c_str());
    return false;
  }
  return true;
}

// ── Layout ──────────────────────────────────────────────────────────────────

int QuotesViewerActivity::getGutterBottom(const GfxRenderer& renderer) const {
  return renderer.getOrientation() == GfxRenderer::Orientation::Portrait ? 75 : 40;
}

int QuotesViewerActivity::getListHeight(const GfxRenderer& renderer) const {
  return renderer.getScreenHeight() - getGutterBottom(renderer) - LINE_HEIGHT;
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

void QuotesViewerActivity::onEnter() {
  Activity::onEnter();
  bookTitle = deriveBookTitle(filePath);
  loadQuotes();
  selectorIndex = 0;
  LOG_DBG("QV", "Loaded %d quotes from %s", static_cast<int>(quotes.size()), filePath.c_str());
  requestUpdate();
}

void QuotesViewerActivity::onExit() { Activity::onExit(); }

// ── Input ───────────────────────────────────────────────────────────────────

void QuotesViewerActivity::loop() {
  const int totalItems = static_cast<int>(quotes.size());

  // Delete confirmation mode.
  if (confirmingDelete >= DELETE_MODE_DISPLAY) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (confirmingDelete == DELETE_MODE_DISPLAY) {
        confirmingDelete = DELETE_MODE_CONFIRM;  // first release arms; text updates to Delete
        requestUpdate();
        return;
      }
      if (selectorIndex >= 0 && selectorIndex < totalItems) {
        quotes.erase(quotes.begin() + selectorIndex);
        saveQuotes();
        if (selectorIndex >= static_cast<int>(quotes.size()) && selectorIndex > 0) selectorIndex--;
      }
      confirmingDelete = DELETE_MODE_OFF;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      confirmingDelete = DELETE_MODE_OFF;
      requestUpdate();
      return;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  // Hold Confirm to enter delete confirmation.
  if (totalItems > 0 && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() > ENTER_DELETE_MODE_MS) {
    confirmingDelete = DELETE_MODE_DISPLAY;
    requestUpdate();
    return;
  }

  if (totalItems == 0) return;

  buttonNavigator.onNextPress([this, totalItems] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, totalItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousPress([this, totalItems] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, totalItems);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, totalItems] {
    selectorIndex =
        ButtonNavigator::nextPageIndex(selectorIndex, totalItems, GUI.getListPageItems(getListHeight(renderer), true));
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, totalItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems,
                                                       GUI.getListPageItems(getListHeight(renderer), true));
    requestUpdate();
  });
}

// ── Render ──────────────────────────────────────────────────────────────────

void QuotesViewerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 40 : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int contentY = isPortraitInverted ? 50 : 0;
  const int listY = contentY + LINE_HEIGHT;
  const int listHeight = getListHeight(renderer);
  const int hintGutterBottom = getGutterBottom(renderer);
  const int totalItems = static_cast<int>(quotes.size());

  // Header: book title + count.
  const std::string header = bookTitle + "  (" + std::to_string(totalItems) + ")";
  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, header.c_str(), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, std::max(contentX + 4, titleX), 15 + contentY, header.c_str(), true,
                    EpdFontFamily::BOLD);

  const auto rowTitle = [this](int index) { return quotes.at(index).text; };
  const auto rowSubtitle = [this](int index) {
    const auto& c = quotes.at(index).chapter;
    return c.empty() ? std::string() : ("[" + c + "]");
  };
  const auto rowIcon = [](int) { return UIIcon::None; };

  if (totalItems > 0) {
    if (confirmingDelete >= DELETE_MODE_DISPLAY) {
      GUI.drawHelpText(renderer, Rect{contentX, pageHeight / 2 - LINE_HEIGHT * 2, contentWidth, LINE_HEIGHT},
                       tr(STR_CONFIRM_DELETE_QUOTE));
      // Show only the selected quote for confirmation.
      const auto onlyTitle = [this](int) { return quotes.at(selectorIndex).text; };
      const auto onlySubtitle = [this](int) {
        const auto& c = quotes.at(selectorIndex).chapter;
        return c.empty() ? std::string() : ("[" + c + "]");
      };
      GUI.drawList(renderer, Rect{contentX, pageHeight / 2, contentWidth, LINE_HEIGHT}, 1, 0, onlyTitle, onlySubtitle,
                   rowIcon);
    } else {
      GUI.drawList(renderer, Rect{contentX, listY, contentWidth, listHeight}, totalItems, selectorIndex, rowTitle,
                   rowSubtitle, rowIcon);
      GUI.drawHelpText(renderer, Rect{contentX, pageHeight - hintGutterBottom, contentWidth, LINE_HEIGHT},
                       tr(STR_HOLD_TO_DELETE));
    }
  } else {
    GUI.drawHelpText(renderer, Rect{contentX, pageHeight / 2, contentWidth, LINE_HEIGHT}, tr(STR_NO_QUOTES));
  }

  const auto backLabel = confirmingDelete >= DELETE_MODE_DISPLAY ? tr(STR_CANCEL) : tr(STR_BACK);
  const auto confirmLabel = (totalItems > 0 && confirmingDelete >= DELETE_MODE_DISPLAY) ? tr(STR_DELETE) : "";
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.present(RefreshIntent::MenuNav);
}
