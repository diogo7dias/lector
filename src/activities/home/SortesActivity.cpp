#include "SortesActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <esp_random.h>

#include "RecentBooksStore.h"
#include "activities/ActivityManager.h"
#include "components/BusyBanner.h"
#include "util/ClippingsReader.h"
#include "util/Sortes.h"

namespace {
constexpr char CLIPPINGS_PATH[] = "/My Clippings.txt";
}  // namespace

void SortesActivity::onEnter() {
  UiStatusActivity::onEnter();
  roll();
}

void SortesActivity::roll() {
  hasItem = false;
  heading.clear();
  subtitle.clear();
  bodyText.clear();
  bookPath.clear();
  setChoiceIndex(0);

  if (mode == Mode::UnreadRoulette) {
    rollUnread();
  } else {
    rollFlashback();
  }
  requestUpdate();
}

void SortesActivity::rollUnread() {
  BusyBanner banner(renderer, tr(STR_SORTES_ROULETTE));
  sortes::UnreadPick pick;
  if (sortes::findUnreadBook(pick, esp_random) != sortes::ScanResult::Found) return;

  hasItem = true;
  bookPath = std::move(pick.path);
  heading = std::move(pick.title);
  subtitle = std::move(pick.author);

  if (FsHelpers::hasEpubExtension(bookPath)) {
    Epub epub(bookPath, "/.crosspoint");
    if (epub.load(/*buildIfMissing=*/false, /*skipLoadingCss=*/true)) {
      if (heading.empty()) heading = epub.getTitle();
      if (subtitle.empty()) subtitle = epub.getAuthor();
      bodyText = epub.getDescription();
      if (bodyText.size() > 360) {
        bodyText.resize(360);
        bodyText += "...";
      }
    }
  }
}

void SortesActivity::rollFlashback() {
  HalFile file;
  if (!Storage.openFileForRead("SORTES", CLIPPINGS_PATH, file)) return;
  const uint64_t fileSize = file.fileSize64();
  if (fileSize == 0) return;

  clippings::Record clip;
  const uint64_t seed = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
  auto readChunk = [](void* ctx, const uint64_t offset, char* dst, const size_t len) -> size_t {
    auto* f = static_cast<HalFile*>(ctx);
    if (!f->seek64(offset)) return 0;
    const int n = f->read(dst, len);
    return n > 0 ? static_cast<size_t>(n) : 0;
  };
  if (!clippings::pickAtOffset(fileSize, clippings::offsetForSeed(seed, fileSize), readChunk, &file, clip)) return;

  hasItem = true;
  heading = std::move(clip.title);
  subtitle = std::move(clip.author);
  bodyText = "\"" + clip.quote + "\"";

  if (!sortes::findBookByTitle(heading, bookPath)) {
    for (const auto& recent : RECENT_BOOKS.getBooks()) {
      if (!recent.title.empty() && strcasecmp(recent.title.c_str(), heading.c_str()) == 0 &&
          Storage.exists(recent.path.c_str())) {
        bookPath = recent.path;
        break;
      }
    }
  }
}

UiStatusActivity::StatusView SortesActivity::statusView() const {
  StatusView view;
  view.title = mode == Mode::UnreadRoulette ? tr(STR_SORTES_ROULETTE) : tr(STR_SORTES_FLASHBACK);

  if (!hasItem) {
    view.lines = {mode == Mode::UnreadRoulette ? tr(STR_SORTES_NO_UNREAD) : tr(STR_SORTES_NO_QUOTES), nullptr, nullptr,
                  nullptr};
    return view;
  }

  view.sections[0].heading = heading.c_str();
  if (!subtitle.empty()) view.sections[0].lines[0] = subtitle.c_str();
  if (!bodyText.empty()) {
    view.sections[1].paragraph = bodyText.c_str();
    view.sections[1].paragraphMaxLines = 8;
  }

  if (mode == Mode::UnreadRoulette) {
    view.choices = {tr(STR_SORTES_START), tr(STR_SORTES_ROLL_AGAIN)};
  } else if (!bookPath.empty()) {
    view.choices = {tr(STR_SORTES_JUMP_TO_BOOK), tr(STR_SORTES_ROLL_AGAIN)};
  } else {
    view.choices = {tr(STR_SORTES_ROLL_AGAIN), nullptr};
  }
  view.confirmHint = tr(STR_SELECT);
  view.thirdHint = tr(STR_DIR_UP);
  view.fourthHint = tr(STR_DIR_DOWN);
  return view;
}

void SortesActivity::onChoiceActivated(const int index) {
  if (!hasItem) return;
  if (mode == Mode::UnreadRoulette) {
    if (index == 0 && !bookPath.empty()) {
      activityManager.goToReader(bookPath);
      return;
    }
    roll();
    return;
  }
  if (!bookPath.empty() && index == 0) {
    activityManager.goToReader(bookPath);
    return;
  }
  roll();
}
