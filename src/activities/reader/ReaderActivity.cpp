#include "ReaderActivity.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Memory.h>

#include <optional>

#include "CrossPointSettings.h"
#include "Epub.h"
#include "EpubReaderActivity.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "Txt.h"
#include "TxtReaderActivity.h"
#include "Xtc.h"
#include "XtcReaderActivity.h"
#include "activities/util/BmpViewerActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "activities/util/PxcViewerActivity.h"

bool ReaderActivity::isXtcFile(const std::string& path) { return FsHelpers::hasXtcExtension(path); }

bool ReaderActivity::isTxtFile(const std::string& path) {
  return FsHelpers::hasTxtExtension(path) ||
         FsHelpers::hasMarkdownExtension(path);  // Treat .md as txt files (until we have a markdown reader)
}

bool ReaderActivity::isBmpFile(const std::string& path) { return FsHelpers::hasBmpExtension(path); }

bool ReaderActivity::isPxcFile(const std::string& path) { return FsHelpers::checkFileExtension(path, ".pxc"); }

std::unique_ptr<Epub> ReaderActivity::loadEpub(GfxRenderer& renderer, const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto epub = makeUniqueNoThrow<Epub>(path, "/.crosspoint");
  if (!epub) {
    LOG_ERR("READER", "Failed to allocate EPUB object");
    return nullptr;
  }
  // First open builds book.bin (container parse + spine/TOC index) — the zip
  // inflate there is exactly what InflateStream's buildscratch claim feeds on.
  const bool uncached = !Storage.exists((epub->getCachePath() + "/book.bin").c_str());
  bool loaded;
  {
    // Lend the framebuffer to the index build; the panel holds its current
    // image, and whichever reader activity follows redraws the full screen.
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
  activityManager.replaceActivity(makeUniqueNoThrow<EpubReaderActivity>(renderer, mappedInput, std::move(epub)));
}

void ReaderActivity::onGoToPxcViewer(const std::string& path) {
  activityManager.replaceActivity(makeUniqueNoThrow<PxcViewerActivity>(renderer, mappedInput, path));
}

void ReaderActivity::onGoToBmpViewer(const std::string& path) {
  activityManager.replaceActivity(makeUniqueNoThrow<BmpViewerActivity>(renderer, mappedInput, path));
}

void ReaderActivity::onGoToXtcReader(std::unique_ptr<Xtc> xtc) {
  const auto xtcPath = xtc->getPath();
  currentBookPath = xtcPath;
  activityManager.replaceActivity(makeUniqueNoThrow<XtcReaderActivity>(renderer, mappedInput, std::move(xtc)));
}

void ReaderActivity::onGoToTxtReader(std::unique_ptr<Txt> txt) {
  const auto txtPath = txt->getPath();
  currentBookPath = txtPath;
  activityManager.replaceActivity(makeUniqueNoThrow<TxtReaderActivity>(renderer, mappedInput, std::move(txt)));
}

void ReaderActivity::onEnter() {
  Activity::onEnter();

  if (initialBookPath.empty()) {
    goToLibrary();  // Start from root when entering via Browse
    return;
  }

  sdFontSystem.ensureLoaded(renderer);

  currentBookPath = initialBookPath;
  if (isBmpFile(initialBookPath)) {
    onGoToBmpViewer(initialBookPath);
  } else if (isPxcFile(initialBookPath)) {
    onGoToPxcViewer(initialBookPath);
  } else {
    // Real book (epub/txt/xtc). Optionally relocate it into /recents/ before it
    // is opened — nothing has a handle on the file or its cache yet. The move
    // carries reading progress + saved quotes; a no-op (feature off, already in
    // /recents/, or a failed move) returns the original path unchanged.
    const std::string bookPath = RECENT_BOOKS.relocateOpenedBookToRecents(initialBookPath);
    currentBookPath = bookPath;
    if (isXtcFile(bookPath)) {
      auto xtc = loadXtc(bookPath);
      if (!xtc) {
        onGoBack();
        return;
      }
      onGoToXtcReader(std::move(xtc));
    } else if (isTxtFile(bookPath)) {
      auto txt = loadTxt(bookPath);
      if (!txt) {
        onGoBack();
        return;
      }
      onGoToTxtReader(std::move(txt));
    } else {
      auto epub = loadEpub(renderer, bookPath);
      if (!epub) {
        onGoBack();
        return;
      }
      onGoToEpubReader(std::move(epub));
    }
  }
}

void ReaderActivity::onGoBack() { finish(); }
