#include "ReaderActivity.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <optional>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "Epub.h"
#include "EpubReaderActivity.h"
#include "KOReaderAutoSync.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "Txt.h"
#include "TxtReaderActivity.h"
#include "Xtc.h"
#include "XtcReaderActivity.h"
#include "activities/boot_sleep/PxcSleepRenderer.h"
#include "activities/util/BmpViewerActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "activities/util/PxcViewerActivity.h"
#include "components/BusyBanner.h"
#include "components/UITheme.h"

bool ReaderActivity::isXtcFile(const std::string& path) { return FsHelpers::hasXtcExtension(path); }

bool ReaderActivity::isTxtFile(const std::string& path) {
  return FsHelpers::hasTxtExtension(path) ||
         FsHelpers::hasMarkdownExtension(path);  // Treat .md as txt files (until we have a markdown reader)
}

bool ReaderActivity::isImageFile(const std::string& path) {
  return FsHelpers::hasBmpExtension(path) || FsHelpers::hasPngExtension(path);
}

int ReaderActivity::initialRefreshCountdown() const {
  if (!allowFastInitialRefresh) return 0;

  const int refreshFrequency = SETTINGS.getRefreshFrequency();
  return refreshFrequency > 1 ? refreshFrequency : 2;
}

std::unique_ptr<Epub> ReaderActivity::loadEpub(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto epub = makeUniqueNoThrow<Epub>(path, "/.crosspoint");
  if (!epub) {
    LOG_ERR("READER", "Failed to allocate EPUB object");
    return nullptr;
  }
  // First open: building the spine/TOC index (book.bin) takes a couple of seconds. Show the
  // indexing popup so it isn't a silent wait on the home screen. The cachePath/hash is known at
  // construction, so this check is valid before load(); a cached open loads in a blink -> no popup.
  const bool uncached = !Storage.exists((epub->getCachePath() + "/book.bin").c_str());
  std::optional<BusyBanner> banner;
  if (uncached) {
    // The banner replaces the restored Quick Resume frame, so the reader must clean it
    // rather than paint the first page differentially over what is no longer there.
    allowFastInitialRefresh = false;
    // Known slow every time, so it skips the banner's usual delay. Kept alive
    // across the load below so any busy::tick() inside the parse still lands on
    // this banner rather than an empty handler.
    banner.emplace(renderer, tr(STR_INDEXING));
    banner->showNow();
  }
  bool loaded;
  {
    // Lend the framebuffer's 48 KB to the container parse (expat + spine/TOC
    // build). The popup just displayed stays on the panel; whichever reader
    // activity follows redraws the full screen anyway.
    std::optional<GfxRenderer::FrameBufferLoan> loan;
    if (uncached) loan.emplace(renderer);
    loaded = epub->load(true, SETTINGS.embeddedStyle == 0);
  }
  if (loaded) {
    return epub;
  }

  LOG_ERR("READER", "Failed to load epub");
  return nullptr;
}

std::unique_ptr<Xtc> ReaderActivity::loadXtc(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto xtc = makeUniqueNoThrow<Xtc>(path, "/.crosspoint");
  if (!xtc) {
    LOG_ERR("READER", "Failed to allocate XTC object");
    return nullptr;
  }
  if (xtc->load()) {
    return xtc;
  }

  LOG_ERR("READER", "Failed to load XTC");
  return nullptr;
}

std::unique_ptr<Txt> ReaderActivity::loadTxt(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto txt = makeUniqueNoThrow<Txt>(path, "/.crosspoint");
  if (!txt) {
    LOG_ERR("READER", "Failed to allocate TXT object");
    return nullptr;
  }
  if (txt->load()) {
    return txt;
  }

  LOG_ERR("READER", "Failed to load TXT");
  return nullptr;
}

void ReaderActivity::goToLibrary(const std::string& fromBookPath) {
  // If coming from a book, start in that book's folder; otherwise start from root
  auto initialPath = fromBookPath.empty() ? "/" : FsHelpers::extractFolderPath(fromBookPath);
  activityManager.goToFileBrowser(std::move(initialPath));
}

void ReaderActivity::onGoToEpubReader(std::unique_ptr<Epub> epub) {
  const auto epubPath = epub->getPath();
  currentBookPath = epubPath;
  activityManager.replaceActivity(
      std::make_unique<EpubReaderActivity>(renderer, mappedInput, std::move(epub), initialRefreshCountdown()));
}

void ReaderActivity::onGoToBmpViewer(const std::string& path) {
  activityManager.replaceActivity(std::make_unique<BmpViewerActivity>(renderer, mappedInput, path));
}

void ReaderActivity::onGoToPxcViewer(const std::string& path) {
  activityManager.replaceActivity(std::make_unique<PxcViewerActivity>(renderer, mappedInput, path));
}

void ReaderActivity::onGoToXtcReader(std::unique_ptr<Xtc> xtc) {
  const auto xtcPath = xtc->getPath();
  currentBookPath = xtcPath;
  activityManager.replaceActivity(
      std::make_unique<XtcReaderActivity>(renderer, mappedInput, std::move(xtc), initialRefreshCountdown()));
}

void ReaderActivity::onGoToTxtReader(std::unique_ptr<Txt> txt) {
  const auto txtPath = txt->getPath();
  currentBookPath = txtPath;
  activityManager.replaceActivity(
      std::make_unique<TxtReaderActivity>(renderer, mappedInput, std::move(txt), initialRefreshCountdown()));
}

bool ReaderActivity::autoSyncPullBeforeOpen() {
  if (!ko_auto_sync::shouldPullOnBookOpen(KOReaderAutoSync::currentGate())) return false;
  // Already fetched for this book since the last lock. Nothing can have changed on the
  // other device in between, so this open goes straight to the page.
  if (!KOReaderAutoSync::pullIsWorthMaking(initialBookPath)) return false;
  // A position is already waiting: this is the reboot the fetch below asked for, and the
  // reader is about to apply it. Fetching again here would loop forever.
  if (KOReaderAutoSync::hasPendingPullFor(initialBookPath)) return false;

  BusyBanner banner(renderer, tr(STR_AUTO_SYNC_PULLING));
  banner.showNow();

  bool stashed = false;
  if (KOReaderAutoSync::connectSavedWifi()) {
    stashed = KOReaderAutoSync::fetchAndStashRemote(initialBookPath);
  }
  KOReaderAutoSync::stopWifi();
  KOReaderAutoSync::notePullMade(initialBookPath);

  if (!stashed) {
    // Nothing came back. Opening the book on this boot costs the user nothing extra:
    // the handshake that fragmented the heap either never happened or failed early.
    return false;
  }

  // silentRestartToReader() reopens whatever openEpubPath names, which is still the
  // previous book until EpubReaderActivity::onEnter() runs. Point it at this one.
  APP_STATE.openEpubPath = initialBookPath;
  APP_STATE.saveToFile();
  silentRestartToReader();
  return true;  // not reached
}

void ReaderActivity::onEnter() {
  Activity::onEnter();

  if (initialBookPath.empty()) {
    goToLibrary();  // Start from root when entering via Browse
    return;
  }

  {
    // Only paints if a font actually gets read off the card; ensureLoaded returns
    // early in the common case where the right family is already in RAM.
    BusyBanner fontBanner(renderer, tr(STR_BUSY_LOADING_FONT));
    sdFontSystem.ensureLoaded(renderer);
  }

  currentBookPath = initialBookPath;
  if (isImageFile(initialBookPath)) {
    onGoToBmpViewer(initialBookPath);
  } else if (hasPxcExtension(initialBookPath)) {
    onGoToPxcViewer(initialBookPath);
  } else if (isXtcFile(initialBookPath)) {
    auto xtc = loadXtc(initialBookPath);
    if (!xtc) {
      onGoBack();
      return;
    }
    onGoToXtcReader(std::move(xtc));
  } else if (isTxtFile(initialBookPath)) {
    auto txt = loadTxt(initialBookPath);
    if (!txt) {
      onGoBack();
      return;
    }
    onGoToTxtReader(std::move(txt));
  } else {
    // Automatic sync collects the position another device pushed here, before the EPUB is
    // loaded: the TLS handshake gets a clean heap, and the reader that opens afterwards
    // gets one that a handshake has not fragmented. Only EPUBs — KOReader sync has no
    // notion of the other formats.
    if (autoSyncPullBeforeOpen()) return;  // rebooted into this book with a position waiting

    auto epub = loadEpub(initialBookPath);
    if (!epub) {
      onGoBack();
      return;
    }
    onGoToEpubReader(std::move(epub));
  }
}

void ReaderActivity::onGoBack() { finish(); }
