#include <Epub/FootnoteExtractor.h>
#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Print.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include "CrossPointSettings.h"
#include "EpubReaderActivity.h"
#include "EpubReaderFootnotesActivity.h"
#include "MappedInputManager.h"
#include "fontIds.h"

namespace {

constexpr int MAX_POPUP_LINES = 64;

// Print sink that feeds deflated XHTML chunks straight into FootnoteTextExtractor
// and signals early stop as soon as the target #id block closes or hits ~2 KB.
class ExtractorPrint final : public Print {
 public:
  explicit ExtractorPrint(footnote_extract::FootnoteTextExtractor& extractor) : extractor_(extractor) {}

  size_t write(const uint8_t b) override {
    const char c = static_cast<char>(b);
    return extractor_.feed(&c, 1) ? 1 : 0;
  }

  size_t write(const uint8_t* buffer, const size_t size) override {
    return extractor_.feed(reinterpret_cast<const char*>(buffer), size) ? size : 0;
  }

 private:
  footnote_extract::FootnoteTextExtractor& extractor_;
};

}  // namespace

void EpubReaderActivity::computeFootnoteBoxes(const Page& page, const int marginLeft, const int marginTop,
                                              const int fontId) {
  currentPageFootnoteBoxes.clear();
  if (!mappedInput.hasTouch() || currentPageFootnotes.empty()) return;

  currentPageFootnoteBoxes.reserve(currentPageFootnotes.size());
  uint32_t globalWord = 0;
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const PageLine*>(element.get());
    const auto& block = line->getBlock();
    if (!block || !block->valid()) continue;

    const uint32_t lineWords = block->wordCount();
    const uint32_t lineEnd = globalWord + lineWords;

    for (size_t fi = 0; fi < currentPageFootnotes.size(); ++fi) {
      const auto& fn = currentPageFootnotes[fi];
      // Direct-tap hit boxes are for footnote links (#id fragment targets), not plain
      // full-chapter TOC links (<a href="chapter1.xhtml">) which fill the centre menu tap zone.
      if (std::strchr(fn.href, '#') == nullptr) continue;
      const uint32_t fnStart = fn.startWord;
      const uint32_t fnCount = fn.wordCount > 0 ? fn.wordCount : 1u;
      const uint32_t fnEnd = fnStart + fnCount;
      if (fnEnd <= globalWord || fnStart >= lineEnd) continue;

      const uint16_t wBegin = static_cast<uint16_t>(std::max(fnStart, globalWord) - globalWord);
      const uint16_t wEnd = static_cast<uint16_t>(std::min(fnEnd, lineEnd) - globalWord);
      int16_t minX = INT16_MAX;
      int16_t minY = INT16_MAX;
      int16_t maxX = INT16_MIN;
      int16_t maxY = INT16_MIN;
      for (uint16_t wi = wBegin; wi < wEnd; ++wi) {
        const int16_t wx = static_cast<int16_t>(line->xPos + block->wordXpos(wi) + marginLeft);
        const int16_t wy = static_cast<int16_t>(line->yPos + marginTop + block->wordBoxTop(renderer, fontId, wi));
        const int16_t ww = static_cast<int16_t>(block->wordWidth(renderer, fontId, wi));
        const int16_t wh = static_cast<int16_t>(renderer.getLineHeight(block->wordRenderFont(renderer, fontId, wi)));
        if (ww <= 0 || wh <= 0) continue;
        minX = std::min(minX, wx);
        minY = std::min(minY, wy);
        maxX = std::max<int16_t>(maxX, static_cast<int16_t>(wx + ww));
        maxY = std::max<int16_t>(maxY, static_cast<int16_t>(wy + wh));
      }
      if (maxX > minX && maxY > minY) {
        currentPageFootnoteBoxes.push_back(footnote_hit::HitBox{minX, minY, static_cast<int16_t>(maxX - minX),
                                                                static_cast<int16_t>(maxY - minY),
                                                                static_cast<uint8_t>(fi)});
      }
    }
    globalWord = lineEnd;
  }
}

bool EpubReaderActivity::openFootnotePopup(const FootnoteEntry& entry) {
  // Kept only when the card opens: a fallback jump to the note has no menu to come back to.
  const MenuReturn menuReturn = std::exchange(footnotePopupMenuReturn, MenuReturn{});
  if (!epub || !section || footnotePopupOpen || entry.href[0] == '\0') return false;

  std::string targetFile;
  std::string targetFrag;
  if (!footnote_extract::splitHref(entry.href, targetFile, targetFrag)) {
    // Plain chapter link without a #id fragment: fall back to full navigation.
    navigateToHref(entry.href, true);
    return true;
  }

  const bool sameFile = targetFile.empty();
  const int targetSpine = sameFile ? currentSpineIndex : epub->resolveHrefToSpineIndex(entry.href, currentSpineIndex);
  if (targetSpine < 0 || targetSpine >= epub->getSpineItemsCount()) {
    navigateToHref(entry.href, true);
    return true;
  }

  const std::string itemHref = epub->getSpineItem(targetSpine).href;
  footnote_extract::FootnoteTextExtractor extractor(targetFrag, footnote_extract::DEFAULT_MAX_BYTES);
  ExtractorPrint sink(extractor);
  const bool streamed = epub->readItemContentsToStream(itemHref, sink, 512, /*allowEarlyStop=*/true);
  if (!streamed || !extractor.found() || extractor.text().empty()) {
    LOG_DBG("ERS", "Footnote popup fallback to navigation for %s", entry.href);
    navigateToHref(entry.href, true);
    return true;
  }

  auto ui = makeUniqueNoThrow<FootnotePopupUi>(renderer);
  if (!ui) {
    LOG_ERR("ERS", "OOM: footnote popup");
    navigateToHref(entry.href, true);
    return true;
  }
  ui->begin();

  const int wrapWidth = ui->bodyWidth(renderer);
  std::vector<std::string> lines;
  lines.reserve(8);
  const std::string& raw = extractor.text();
  size_t pos = 0;
  while (pos < raw.size() && static_cast<int>(lines.size()) < MAX_POPUP_LINES) {
    const size_t nl = raw.find('\n', pos);
    const size_t end = (nl == std::string::npos) ? raw.size() : nl;
    if (end > pos) {
      std::string para = raw.substr(pos, end - pos);
      const int remaining = MAX_POPUP_LINES - static_cast<int>(lines.size());
      auto wrapped = renderer.wrappedText(UI_10_FONT_ID, para.c_str(), wrapWidth, remaining);
      for (auto& line : wrapped) lines.push_back(std::move(line));
    }
    if (nl == std::string::npos) break;
    pos = nl + 1;
  }
  if (lines.empty()) {
    navigateToHref(entry.href, true);
    return true;
  }
  if (extractor.truncated() && lines.back().size() + 3 <= 256) {
    lines.back().append("...");
  }

  // A linked same-chapter <aside epub:type="footnote"> is kept out of main flow, so only
  // targets that live in rendered flow (another spine item or a non-aside block) can jump.
  const bool canJump = (targetSpine != currentSpineIndex) || !extractor.isFootnoteAside();
  char titleBuf[64];
  if (entry.number[0] != '\0') {
    snprintf(titleBuf, sizeof(titleBuf), "%s %s", tr(STR_FOOTNOTE), entry.number);
  } else {
    snprintf(titleBuf, sizeof(titleBuf), "%s", tr(STR_FOOTNOTE));
  }

  {
    RenderLock lock(*this);
    footnotePopupUi = std::move(ui);
    footnotePopupOpen = true;
    footnotePopupPaintOnly = true;
    footnotePopupCanJump = canJump;
    footnotePopupPage = 0;
    footnotePopupTitle = titleBuf;
    footnotePopupHref = entry.href;
    footnotePopupLines = std::move(lines);
    footnotePopupMenuReturn = menuReturn;
  }
  LOG_INF("ERS", "Opened footnote popup: %s (%zu lines, jump=%d)", entry.href, footnotePopupLines.size(),
          canJump ? 1 : 0);
  requestUpdate();
  return true;
}

void EpubReaderActivity::closeFootnotePopup(const bool repaint) {
  {
    RenderLock lock(*this);
    footnotePopupOpen = false;
    footnotePopupPaintOnly = false;
    footnotePopupCanJump = false;
    footnotePopupPage = 0;
    footnotePopupUi.reset();
    footnotePopupLines.clear();
    footnotePopupLines.shrink_to_fit();
    footnotePopupTitle.clear();
    footnotePopupHref.clear();
    footnotePopupMenuReturn = MenuReturn{};
  }
  if (repaint) {
    scheduleGhostCleanup();
    requestUpdate();
  }
}

void EpubReaderActivity::paintFootnotePopup() {
  if (!footnotePopupOpen || !footnotePopupUi) return;

  const int totalLines = static_cast<int>(footnotePopupLines.size());
  const int noFooterCap = footnotePopupUi->maxLinesPerPage(renderer, false);
  const bool multiPage = totalLines > noFooterCap;
  const bool hasFooter = multiPage || footnotePopupCanJump;
  const int perPage = footnotePopupUi->maxLinesPerPage(renderer, hasFooter);
  const int totalPages = std::max(1, (totalLines + perPage - 1) / perPage);
  if (footnotePopupPage >= totalPages) footnotePopupPage = totalPages - 1;
  if (footnotePopupPage < 0) footnotePopupPage = 0;

  const int startLine = footnotePopupPage * perPage;
  const int count = std::min(perPage, std::max(0, totalLines - startLine));
  const char* linePtrs[32];
  const int safeCount = std::min<int>(count, static_cast<int>(sizeof(linePtrs) / sizeof(linePtrs[0])));
  for (int i = 0; i < safeCount; ++i) {
    linePtrs[i] = footnotePopupLines[static_cast<size_t>(startLine + i)].c_str();
  }

  char pageInfo[16] = {};
  if (totalPages > 1) {
    snprintf(pageInfo, sizeof(pageInfo), "%d/%d", footnotePopupPage + 1, totalPages);
  }

  FootnotePopupUi::Model model;
  model.title = footnotePopupTitle.c_str();
  model.pageInfo = pageInfo;
  model.lines = linePtrs;
  model.lineCount = safeCount;
  // Lock multi-page cards to a constant height so paging overwrites the full previous card cleanly.
  model.reservedLines = totalPages > 1 ? perPage : safeCount;
  model.showPager = totalPages > 1;
  model.hasPrev = footnotePopupPage > 0;
  model.hasNext = footnotePopupPage + 1 < totalPages;
  model.showJump = footnotePopupCanJump;
  model.jumpLabel = tr(STR_JUMP_TO_NOTE);

  footnotePopupUi->setModel(model);
  footnotePopupUi->render();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

bool EpubReaderActivity::handleFootnotePopupInput() {
  if (pendingFootnotePopupIndex >= 0 && section && pageOnScreen.load(std::memory_order_acquire)) {
    const int idx = std::exchange(pendingFootnotePopupIndex, -1);
    if (idx >= 0 && idx < static_cast<int>(currentPageFootnotes.size())) {
      const FootnoteEntry entry = currentPageFootnotes[static_cast<size_t>(idx)];
      openFootnotePopup(entry);
    }
    return true;
  }
  if (!footnotePopupOpen || !footnotePopupUi) return false;

  const int totalLines = static_cast<int>(footnotePopupLines.size());
  const int noFooterCap = footnotePopupUi->maxLinesPerPage(renderer, false);
  const bool hasFooter = (totalLines > noFooterCap) || footnotePopupCanJump;
  const int perPage = footnotePopupUi->maxLinesPerPage(renderer, hasFooter);
  const int totalPages = std::max(1, (totalLines + perPage - 1) / perPage);

  auto stepPage = [this, totalPages](const int delta) {
    const int next = std::clamp(footnotePopupPage + delta, 0, totalPages - 1);
    if (next == footnotePopupPage) return;
    {
      RenderLock lock(*this);
      footnotePopupPage = next;
      footnotePopupPaintOnly = true;
    }
    requestUpdate();
  };

  auto jumpToTarget = [this]() {
    const std::string href = footnotePopupHref;
    closeFootnotePopup(false);
    navigateToHref(href, true);
  };

  const auto routed = footnotePopupUi->route(mappedInput);
  switch (routed.event) {
    case FootnotePopupUi::Event::Dismiss:
      closeFootnotePopup(true);
      return true;
    case FootnotePopupUi::Event::Jump:
      jumpToTarget();
      return true;
    case FootnotePopupUi::Event::PrevPage:
      stepPage(-1);
      return true;
    case FootnotePopupUi::Event::NextPage:
      stepPage(1);
      return true;
    case FootnotePopupUi::Event::None:
      break;
  }
  if (routed.routed) return true;

  using Button = MappedInputManager::Button;
  if (mappedInput.wasPressed(Button::Back) || mappedInput.wasBackGesture()) {
    // Back returns to the screen that opened the card: the menu, when its row did.
    const MenuReturn menuReturn = footnotePopupMenuReturn;
    closeFootnotePopup(true);
    menuReturn.run();
  } else if (mappedInput.wasPressed(Button::Confirm)) {
    if (footnotePopupCanJump) {
      jumpToTarget();
    } else {
      closeFootnotePopup(true);
    }
  } else if (mappedInput.wasPressed(Button::Left) || mappedInput.wasPressed(Button::Up) ||
             mappedInput.wasPressed(Button::PageBack)) {
    stepPage(-1);
  } else if (mappedInput.wasPressed(Button::Right) || mappedInput.wasPressed(Button::Down) ||
             mappedInput.wasPressed(Button::PageForward)) {
    stepPage(1);
  }
  return true;
}

void EpubReaderActivity::openFootnotes() {
  if (currentPageFootnotes.size() == 1) {
    // One footnote: the row opens the card itself, so the card takes the menu's reopen.
    footnotePopupMenuReturn = MenuReturn::take();
    if (pageOnScreen.load(std::memory_order_acquire) && section) {
      openFootnotePopup(currentPageFootnotes[0]);
    } else {
      pendingFootnotePopupIndex = 0;
      requestUpdate();
    }
    return;
  }
  startActivityForResult(
      std::make_unique<EpubReaderFootnotesActivity>(renderer, mappedInput, currentPageFootnotes),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          const auto& footnoteResult = std::get<FootnoteResult>(result.data);
          if (footnoteResult.index >= 0 && footnoteResult.index < static_cast<int>(currentPageFootnotes.size())) {
            pendingFootnotePopupIndex = footnoteResult.index;
          } else {
            navigateToHref(footnoteResult.href, true);
          }
        }
        requestUpdate();
      });
}
