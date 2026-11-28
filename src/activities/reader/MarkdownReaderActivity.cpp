#include "MarkdownReaderActivity.h"

#include <Epub/Page.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <limits>

#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ReaderMargins.h"
#include "RecentBooksStore.h"
#include "RuledLines.h"
#include "SdCardFontSystem.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"
#include "util/BookProgressFile.h"
#include "util/ProgressFile.h"
#include "util/ReadingPercent.h"

std::string MarkdownReaderActivity::readerOverridePath() const { return readerSidecarPath(txt_->getCachePath()); }

void MarkdownReaderActivity::loadReaderPrefs() {
  const BookReaderPrefs book = loadBookReaderPrefs(txt_->getCachePath(), ReaderPrefs::fromGlobal());
  prefs_ = book.prefs;
  prefsCustom_ = book.custom;
  if (book.migrated) writeReaderOverride(prefs_);
}

bool MarkdownReaderActivity::writeReaderOverride(const ReaderPrefs& p) const {
  HalFile f;
  if (!Storage.openFileForWrite("MDR", readerOverridePath(), f)) return false;
  return writeReaderPrefs(f, p);
}

void MarkdownReaderActivity::onEnter() {
  Activity::onEnter();
  if (!txt_) return;

  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  txt_->setupCacheDir();
  loadReaderPrefs();
  sdFontSystem.ensureLoadedFor(renderer, prefs_.sdFontFamilyName, prefs_.fontPointSize);

  const auto filePath = txt_->getPath();
  const auto fileName = filePath.substr(filePath.rfind('/') + 1);
  APP_STATE.openEpubPath = filePath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(filePath, fileName, "");
  requestUpdate();
}

void MarkdownReaderActivity::onExit() {
  Activity::onExit();
  if (auto* fontCache = renderer.getFontCacheManager()) fontCache->releaseSdFontCaches();
  if (txt_ && progressSaveDebouncer_.hasPending()) saveProgress(progressSaveDebouncer_.lastObservedPosition());
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  if (txt_ && totalPages_ > 0 && !pendingDeleteBook_) {
    const int pct = reading_percent::pagePercent(currentPage_, totalPages_);
    RECENT_BOOKS.setProgress(txt_->getPath(), pct);
    book_progress::Marker marker;
    marker.percent = static_cast<uint8_t>(pct);
    marker.readOrder = ++APP_STATE.readOrderCounter;
    book_progress::write(txt_->getCachePath(), marker);
    APP_STATE.saveToFile();
  }

  section_.reset();
  APP_STATE.readerActivityLoadCount = 0;

  const std::string deletePath = pendingDeleteBook_ && txt_ ? txt_->getPath() : std::string();
  txt_.reset();
  if (!deletePath.empty()) deleteOpenedBook(deletePath);
  APP_STATE.saveToFile();
}

void MarkdownReaderActivity::initializeReader() {
  if (initialized_ || !txt_) return;

  const int fontId = SETTINGS.getReaderFontId(prefs_);
  const int8_t charSpacing = static_cast<int8_t>(
      std::min(prefs_.characterSpacing, reader_defaults::MAX_CHARACTER_SPACING) - reader_defaults::CHARACTER_SPACING);
  renderer.setCharacterSpacing(fontId, charSpacing);

  ReaderUtils::readerMargins(renderer, prefs_, marginTop_, marginRight_, marginBottom_, marginLeft_);
  const StatusBarBlock sb = statusBarOf(prefs_);
  bool sbTitleTop = false;
  const int sbTitleLines =
      UITheme::getStatusBarV2TitleLines(sb, renderer, true, "", txt_->getTitle().c_str(), &sbTitleTop);
  const int sbTitleExtraPx = (sbTitleLines - 1) * renderer.getLineHeight(UI_10_FONT_ID);
  marginTop_ += UITheme::getInstance().getStatusBarV2TopHeight(sb, true, sbTitleTop ? sbTitleExtraPx : 0);
  marginBottom_ += UITheme::getInstance().getStatusBarV2BottomHeight(sb, true, sbTitleTop ? 0 : sbTitleExtraPx);

  const int vw = std::max(1, renderer.getScreenWidth() - marginLeft_ - marginRight_);
  const int vh = std::max(1, renderer.getScreenHeight() - marginTop_ - marginBottom_);
  const ReaderRenderSpec spec = makeRenderSpec(prefs_, fontId, static_cast<uint16_t>(vw), static_cast<uint16_t>(vh));
  const auto fileSize = static_cast<uint32_t>(txt_->getFileSize());

  section_ = makeUniqueNoThrow<MarkdownSection>(txt_->getCachePath());
  if (!section_) {
    LOG_ERR("MDR", "OOM: MarkdownSection");
    totalPages_ = 0;
    initialized_ = true;
    return;
  }

  if (!section_->load(spec, fileSize)) {
    if (!quietBuild_) GUI.drawPopup(renderer, tr(STR_INDEXING));
    quietBuild_ = false;
    section_->build(txt_->getPath(), renderer, spec, fileSize);
  }
  totalPages_ = section_->pageCount();
  loadProgress();
  initialized_ = true;
}

void MarkdownReaderActivity::relayoutPreservingPercent() {
  if (!txt_) return;
  const int oldTotal = std::max(1, totalPages_);
  const int oldPage = currentPage_;
  initialized_ = false;
  section_.reset();
  initializeReader();
  if (totalPages_ > 0) {
    currentPage_ = std::clamp((oldPage * totalPages_) / oldTotal, 0, totalPages_ - 1);
  }
  requestUpdate();
}

void MarkdownReaderActivity::loop() {
  if (menuPopup_.handleInput(mappedInput, [this]() { requestUpdate(); })) return;

  backLatch_.observe(mappedInput.wasPressed(MappedInputManager::Button::Back));

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  if (ReaderUtils::isTouchMenuGesture(renderer, mappedInput, touch.menu) ||
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    openMenuPopup();
    return;
  }

  if (ReaderUtils::handleBackNavigation(mappedInput, activityManager, txt_ ? txt_->getPath().c_str() : "",
                                        backLatch_)) {
    return;
  }

  auto [prevTriggered, nextTriggered] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) return;
  pageTurn(nextTriggered);
}

void MarkdownReaderActivity::pageTurn(const bool forward) {
  if (!forward) {
    if (currentPage_ == 0) return;
    currentPage_--;
    requestUpdate();
    return;
  }
  if (currentPage_ < totalPages_ - 1) {
    if (sessionPages_ < std::numeric_limits<int>::max()) ++sessionPages_;
    currentPage_++;
    requestUpdate();
  } else {
    onGoHome();
  }
}

void MarkdownReaderActivity::jumpToAdjacentHeading(const bool forward) {
  if (!section_ || section_->toc().empty()) return;
  const auto& toc = section_->toc();
  int target = -1;
  if (forward) {
    for (const auto& entry : toc) {
      if (entry.pageIndex > currentPage_) {
        target = entry.pageIndex;
        break;
      }
    }
  } else {
    for (auto it = toc.rbegin(); it != toc.rend(); ++it) {
      if (it->pageIndex < currentPage_) {
        target = it->pageIndex;
        break;
      }
    }
  }
  if (target < 0 || target >= totalPages_) return;
  currentPage_ = target;
  pagesUntilFullRefresh_ = 0;
  requestUpdate();
}

std::string MarkdownReaderActivity::currentHeadingTitle() const {
  if (!section_ || section_->toc().empty()) return "";
  std::string title;
  for (const auto& entry : section_->toc()) {
    if (entry.pageIndex > currentPage_) break;
    title = entry.title;
  }
  return title;
}

void MarkdownReaderActivity::render(RenderLock&&) {
  if (!txt_) return;
  if (!initialized_) initializeReader();

  if (!section_ || totalPages_ == 0) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::REGULAR);
    renderer.displayBuffer();
    return;
  }

  currentPage_ = std::clamp(currentPage_, 0, totalPages_ - 1);
  renderer.clearScreen();
  renderPage();
  menuPopup_.processRender(renderer, mappedInput);
  if (progressSaveDebouncer_.observe(currentPage_)) saveProgress(currentPage_);
}

void MarkdownReaderActivity::renderPage() {
  auto page = section_->loadPage(currentPage_);
  if (!page) {
    renderer.drawCenteredText(UI_10_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::REGULAR);
    renderer.displayBuffer();
    return;
  }

  const int fontId = SETTINGS.getReaderFontId(prefs_);
  const int8_t charSpacing = static_cast<int8_t>(
      std::min(prefs_.characterSpacing, reader_defaults::MAX_CHARACTER_SPACING) - reader_defaults::CHARACTER_SPACING);
  renderer.setCharacterSpacing(fontId, charSpacing);

  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  page->render(renderer, fontId, marginLeft_, marginTop_);
  renderStatusBar();
  scope.endScanAndPrewarm();

  renderer.setPaperbackLook(prefs_.paperbackLookBody);
  page->render(renderer, fontId, marginLeft_, marginTop_);
  renderer.setPaperbackLook(false);

  ruled_lines::drawPage(renderer, *page, fontId, readerLineCompression(prefs_.lineSpacingPercent), marginLeft_,
                        marginTop_, marginRight_, marginBottom_);
  renderStatusBar();
  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh_);
}

void MarkdownReaderActivity::renderStatusBar() const {
  StatusBarData d;
  const bool hasToc = section_ && !section_->toc().empty();
  d.hasChapters = hasToc;
  d.chapterTitle = hasToc ? currentHeadingTitle() : "";
  d.chapterPage = currentPage_ + 1;
  d.chapterPages = totalPages_;
  d.bookPercent = reading_percent::pagePercent(currentPage_, totalPages_);
  d.bookTitle = txt_->getTitle();
  d.sessionPages = sessionPages_;
  const StatusBarBlock sb = statusBarOf(prefs_);
  renderer.setPaperbackLook(prefs_.paperbackLookStatus);
  GUI.drawStatusBarV2(renderer, d, sb);
  renderer.setPaperbackLook(false);
}

void MarkdownReaderActivity::saveProgress(const uint32_t page) {
  uint8_t data[4];
  data[0] = page & 0xFF;
  data[1] = (page >> 8) & 0xFF;
  data[2] = 0;
  data[3] = 0;
  if (ProgressFile::writeAtomic(txt_->getCachePath(), data, sizeof(data))) {
    progressSaveDebouncer_.markPersisted(page);
  }
}

void MarkdownReaderActivity::loadProgress() {
  HalFile f;
  if (ProgressFile::openForRead("MDR", txt_->getCachePath(), f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage_ = std::clamp<int>(data[0] + (data[1] << 8), 0, std::max(0, totalPages_ - 1));
    }
  }
}

ScreenshotInfo MarkdownReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Txt;
  if (txt_) {
    const std::string t = txt_->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
  }
  info.currentPage = currentPage_ + 1;
  info.totalPages = totalPages_;
  info.progressPercent = reading_percent::pagePercent(currentPage_, totalPages_);
  return info;
}
