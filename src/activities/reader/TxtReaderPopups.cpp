#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderPresetMigration.h"
#include "ReaderPresetStore.h"
#include "ReaderPresetsActivity.h"
#include "ReaderUtils.h"
#include "SdCardFontSystem.h"
#include "TxtReaderActivity.h"
#include "activities/settings/TextSettingsActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "util/BookFilingNames.h"

std::string TxtReaderActivity::readerOverridePath() const { return readerSidecarPath(txt->getCachePath()); }

void TxtReaderActivity::loadReaderPrefs() {
  const BookReaderPrefs book = loadBookReaderPrefs(txt->getCachePath(), ReaderPrefs::fromGlobal());
  prefs_ = book.prefs;
  prefsCustom_ = book.custom;
  if (book.migrated) writeReaderOverride(prefs_);
}

bool TxtReaderActivity::writeReaderOverride(const ReaderPrefs& p) const {
  HalFile f;
  if (!Storage.openFileForWrite("TRS", readerOverridePath(), f)) {
    LOG_ERR("TRS", "Failed to open reader_override.bin for write");
    return false;
  }
  if (!writeReaderPrefs(f, p)) {
    LOG_ERR("TRS", "Short write to reader_override.bin");
    return false;
  }
  return true;
}

void TxtReaderActivity::readerEditSinkThunk(void* ctx, const ReaderPrefs& live) {
  auto* self = static_cast<TxtReaderActivity*>(ctx);
  self->readerEdit_ = live;
  self->persistReaderSettingsEdit(live);
}

void TxtReaderActivity::persistReaderSettingsEdit(const ReaderPrefs& live) const {
  const ReaderOverrideDecision decision = decideReaderOverride(live, prefs_, prefsCustom_);
  switch (decision.action) {
    case ReaderOverrideAction::Write:
      writeReaderOverride(decision.prefs);
      break;
    case ReaderOverrideAction::Remove:
      Storage.remove(readerOverridePath().c_str());
      break;
    case ReaderOverrideAction::Keep:
      break;
  }
}

void TxtReaderActivity::applyReaderSettingsEdit() {
  ReaderPrefs edited = readerEdit_;
  restoreBookOnlyFields(edited, prefs_);
  if (std::memcmp(&edited, &prefs_, sizeof(ReaderPrefs)) == 0) {
    requestUpdate();
    return;
  }
  prefs_ = edited;
  prefsCustom_ = true;
  writeReaderOverride(prefs_);
  sdFontSystem.ensureLoadedFor(renderer, prefs_.sdFontFamilyName, prefs_.fontPointSize);
  quietIndex_ = true;
  pagesUntilFullRefresh = 0;
  relayoutForFontChange();
}

ReaderPrefs TxtReaderActivity::applyReaderPrefsFrom(const ReaderPrefs& incoming) {
  ReaderPrefs next = incoming;
  if (next.sdFontFamilyName[0] != '\0') {
    const auto& families = sdFontSystem.registry().getFamilies();
    const bool installed = std::any_of(families.begin(), families.end(), [&](const SdCardFontFamilyInfo& fam) {
      return fam.name == next.sdFontFamilyName;
    });
    if (!installed) {
      LOG_ERR("TRS", "Reader prefs name a missing SD font '%s' — falling back to built-in", next.sdFontFamilyName);
      std::memset(next.sdFontFamilyName, 0, sizeof(next.sdFontFamilyName));
      next.fontFamily = CrossPointSettings::CHAREINK;
    }
  }
  if (std::memcmp(&next, &prefs_, sizeof(ReaderPrefs)) == 0) {
    requestUpdate();
    return next;
  }
  prefs_ = next;
  prefsCustom_ = true;
  writeReaderOverride(prefs_);
  sdFontSystem.ensureLoadedFor(renderer, prefs_.sdFontFamilyName, prefs_.fontPointSize);
  quietIndex_ = true;
  pagesUntilFullRefresh = 0;
  relayoutForFontChange();
  return next;
}

void TxtReaderActivity::resetReaderPrefsToGlobal() {
  Storage.remove(readerOverridePath().c_str());
  prefs_ = ReaderPrefs::fromGlobal();
  prefsCustom_ = false;
  sdFontSystem.ensureLoadedFor(renderer, prefs_.sdFontFamilyName, prefs_.fontPointSize);
  quietIndex_ = true;
  pagesUntilFullRefresh = 0;
  relayoutForFontChange();
}

void TxtReaderActivity::openReaderSettings() {
  readerEdit_ = prefs_;
  startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(), prefs_,
                                                                &TxtReaderActivity::readerEditSinkThunk, this),
                         [this](const ActivityResult&) { applyReaderSettingsEdit(); });
}

void TxtReaderActivity::openReadingThemes() {
  startActivityForResult(makeUniqueNoThrow<ReaderPresetsActivity>(renderer, mappedInput, prefs_),
                         [this](const ActivityResult& res) {
                           const auto* pr = res.isCancelled ? nullptr : std::get_if<PresetResult>(&res.data);
                           const ReaderPreset* preset = pr ? READER_PRESETS.get(pr->index) : nullptr;
                           if (!preset) {
                             requestUpdate();
                             return;
                           }
                           ReaderPrefs stored = preset->prefs;
                           reader_preset_migration::keepStatusBarWhenAbsent(preset->carriesStatusBar, prefs_, stored);
                           const ReaderPrefs adopted = applyReaderPrefsFrom(stored);
                           if (std::memcmp(&adopted, &stored, sizeof(ReaderPrefs)) != 0) {
                             READER_PRESETS.overwrite(pr->index, adopted);
                           }
                         });
}

void TxtReaderActivity::askResetReaderSettings() {
  if (!txt) return;
  const std::string name = std::string(bookfiling::fileNameOf(txt->getPath()));
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput,
                                                                tr(STR_RESET_READER_SETTINGS) + std::string("?"), name),
                         [this](const ActivityResult& res) {
                           if (res.isCancelled) {
                             requestUpdate();
                             return;
                           }
                           resetReaderPrefsToGlobal();
                         });
}

void TxtReaderActivity::openSettingsPopup() {
  enum class Action : uint8_t { TextSettings, ReadingThemes, ResetSettings, DeleteBook };
  std::vector<std::string> rows;
  std::vector<Action> actions;
  rows.reserve(4);
  actions.reserve(4);
  rows.push_back(tr(STR_TEXT_SETTINGS));
  actions.push_back(Action::TextSettings);
  rows.push_back(tr(STR_READING_THEMES));
  actions.push_back(Action::ReadingThemes);
  if (prefsCustom_) {
    rows.push_back(tr(STR_RESET_READER_SETTINGS));
    actions.push_back(Action::ResetSettings);
  }
  rows.push_back(tr(STR_DELETE_BOOK));
  actions.push_back(Action::DeleteBook);

  settingsPopup.show(StrId::STR_BOOK_MENU, rows, 0, [this, actions = std::move(actions)](const int idx) {
    if (idx < 0 || idx >= static_cast<int>(actions.size())) return;
    switch (actions[idx]) {
      case Action::TextSettings:
        openReaderSettings();
        break;
      case Action::ReadingThemes:
        openReadingThemes();
        break;
      case Action::ResetSettings:
        askResetReaderSettings();
        break;
      case Action::DeleteBook:
        askDeleteBook();
        break;
    }
  });
  requestUpdate();
}

void TxtReaderActivity::relayoutForFontChange() {
  if (!txt) return;
  // Page numbers mean nothing across a font change, but byte offsets do: remember the
  // byte the reader is showing, then land on whichever new page contains it.
  // On the End of Book screen currentPage is one past the last page: anchor on the last
  // page there, and go back to the end screen once the new index is built.
  const bool atEnd = !pageOffsets.empty() && currentPage >= static_cast<int>(pageOffsets.size());
  const size_t anchorOffset =
      pageOffsets.empty() ? 0 : pageOffsets[std::min<size_t>(currentPage, pageOffsets.size() - 1)];

  initialized = false;
  pageOffsets.clear();
  currentPageLines.clear();
  initializeReader();

  // loadProgress() inside initializeReader restores the page number saved for the old
  // font, so the anchor is applied after it, not before.
  int landing = 0;
  for (size_t i = 0; i < pageOffsets.size(); i++) {
    if (pageOffsets[i] > anchorOffset) break;
    landing = static_cast<int>(i);
  }
  currentPage = atEnd ? totalPages : landing;
  requestUpdate();
}

bool TxtReaderActivity::runBoundAction(const uint8_t function) {
  // The pop-up owns the buttons while it is up; a bound gesture must not act underneath it.
  if (settingsPopup.isActive()) return true;

  if (function == CrossPointSettings::LP_MENU_PAGE_PREV || function == CrossPointSettings::LP_MENU_PAGE_NEXT) {
    pageTurn(function == CrossPointSettings::LP_MENU_PAGE_NEXT);
    return true;
  }

  if (function == CrossPointSettings::LP_MENU_NEXT_CHAPTER || function == CrossPointSettings::LP_MENU_PREV_CHAPTER) {
    skipPages(function == CrossPointSettings::LP_MENU_NEXT_CHAPTER ? 10 : -10);
    return true;
  }

  if (function == CrossPointSettings::LP_MENU_OPEN_READER_MENU) {
    openSettingsPopup();  // what Confirm opens here
    return true;
  }

  if (function == CrossPointSettings::LP_MENU_TEXT_SETTINGS ||
      function == CrossPointSettings::LP_MENU_READER_SETTINGS) {
    openReaderSettings();
    return true;
  }

  if (function == CrossPointSettings::LP_MENU_ALIGN_LEFT || function == CrossPointSettings::LP_MENU_ALIGN_JUSTIFY) {
    const uint8_t alignment = function == CrossPointSettings::LP_MENU_ALIGN_LEFT ? CrossPointSettings::LEFT_ALIGN
                                                                                 : CrossPointSettings::JUSTIFIED;
    if (prefs_.paragraphAlignment == alignment) return true;
    prefs_.paragraphAlignment = alignment;
    prefsCustom_ = true;
    writeReaderOverride(prefs_);
    quietIndex_ = true;  // straight back to the page, no Indexing popup
    pagesUntilFullRefresh = 0;
    relayoutForFontChange();  // re-reads the alignment and keeps the byte on screen
    return true;
  }

  switch (simple_reader_shortcut::resolve(function, /*supportsStatusBarToggle=*/true)) {
    case simple_reader_shortcut::Action::ToggleStatusBar:
      prefs_.statusBarEnabled = prefs_.statusBarEnabled ? 0 : 1;
      prefsCustom_ = true;
      writeReaderOverride(prefs_);
      quietIndex_ = true;
      pagesUntilFullRefresh = 0;
      relayoutForFontChange();
      return true;
    case simple_reader_shortcut::Action::WallpaperHold:
      ReaderUtils::toggleWallpaperHold(renderer, pagesUntilFullRefresh);
      requestUpdate();
      return true;
    case simple_reader_shortcut::Action::None:
      break;
  }
  // Not runnable in this reader, or runnable only when a card read says so (Hold
  // Wallpaper checks the file is still there). Fall through to the shared handler, which
  // answers the device-wide actions; anything else is a dead press.
  return false;
}

void TxtReaderActivity::askDeleteBook() {
  if (!txt) return;
  const std::string path = txt->getPath();
  // The file name, deliberately, where the EPUB prompt uses the book's title: a TXT
  // "title" is only the file name with its extension removed, so naming the file is both
  // the same information and the more precise answer to "which file is about to go".
  const std::string name = std::string(bookfiling::fileNameOf(path));
  // Deleting a book file is not undoable, so it asks first, the same as deleting a
  // sleep wallpaper does.
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_BOOK) + std::string("?"), name),
      [this](const ActivityResult& res) {
        if (res.isCancelled) {
          requestUpdate();
          return;
        }
        pendingDeleteBook = true;
        onGoHome();
      });
}
