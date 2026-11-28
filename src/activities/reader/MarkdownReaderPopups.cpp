#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "MarkdownReaderActivity.h"
#include "ReaderPresetMigration.h"
#include "ReaderPresetStore.h"
#include "ReaderPresetsActivity.h"
#include "SdCardFontSystem.h"
#include "activities/settings/TextSettingsActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "util/BookFilingNames.h"

void MarkdownReaderActivity::readerEditSinkThunk(void* ctx, const ReaderPrefs& live) {
  auto* self = static_cast<MarkdownReaderActivity*>(ctx);
  self->readerEdit_ = live;
  self->persistReaderSettingsEdit(live);
}

void MarkdownReaderActivity::persistReaderSettingsEdit(const ReaderPrefs& live) const {
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

void MarkdownReaderActivity::applyReaderSettingsEdit() {
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
  quietBuild_ = true;
  pagesUntilFullRefresh_ = 0;
  relayoutPreservingPercent();
}

ReaderPrefs MarkdownReaderActivity::applyReaderPrefsFrom(const ReaderPrefs& incoming) {
  ReaderPrefs next = incoming;
  if (next.sdFontFamilyName[0] != '\0') {
    const auto& families = sdFontSystem.registry().getFamilies();
    const bool installed = std::any_of(families.begin(), families.end(), [&](const SdCardFontFamilyInfo& fam) {
      return fam.name == next.sdFontFamilyName;
    });
    if (!installed) {
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
  quietBuild_ = true;
  pagesUntilFullRefresh_ = 0;
  relayoutPreservingPercent();
  return next;
}

void MarkdownReaderActivity::resetReaderPrefsToGlobal() {
  Storage.remove(readerOverridePath().c_str());
  prefs_ = ReaderPrefs::fromGlobal();
  prefsCustom_ = false;
  sdFontSystem.ensureLoadedFor(renderer, prefs_.sdFontFamilyName, prefs_.fontPointSize);
  quietBuild_ = true;
  pagesUntilFullRefresh_ = 0;
  relayoutPreservingPercent();
}

void MarkdownReaderActivity::openTocPopup() {
  if (!section_ || section_->toc().empty()) return;
  const auto& toc = section_->toc();
  std::vector<std::string> rows;
  rows.reserve(toc.size());
  int currentIdx = 0;
  for (size_t i = 0; i < toc.size(); ++i) {
    if (toc[i].pageIndex <= currentPage_) currentIdx = static_cast<int>(i);
    rows.push_back(toc[i].level >= 2 ? ("  " + toc[i].title) : toc[i].title);
  }
  menuPopup_.show(StrId::STR_SELECT_CHAPTER, rows, currentIdx, [this](const int idx) {
    if (!section_ || idx < 0 || idx >= static_cast<int>(section_->toc().size())) return;
    currentPage_ = std::clamp<int>(section_->toc()[idx].pageIndex, 0, std::max(0, totalPages_ - 1));
    pagesUntilFullRefresh_ = 0;
    requestUpdate();
  });
  requestUpdate();
}

void MarkdownReaderActivity::openReaderSettings() {
  readerEdit_ = prefs_;
  startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(), prefs_,
                                                                &MarkdownReaderActivity::readerEditSinkThunk, this),
                         [this](const ActivityResult&) { applyReaderSettingsEdit(); });
}

void MarkdownReaderActivity::openReadingThemes() {
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

void MarkdownReaderActivity::askResetReaderSettings() {
  if (!txt_) return;
  const std::string name = std::string(bookfiling::fileNameOf(txt_->getPath()));
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

void MarkdownReaderActivity::askDeleteBook() {
  if (!txt_) return;
  const std::string name = std::string(bookfiling::fileNameOf(txt_->getPath()));
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_BOOK) + std::string("?"), name),
      [this](const ActivityResult& res) {
        if (res.isCancelled) {
          requestUpdate();
          return;
        }
        pendingDeleteBook_ = true;
        onGoHome();
      });
}

void MarkdownReaderActivity::openMenuPopup() {
  enum class Action : uint8_t { Chapters, TextSettings, ReadingThemes, ResetSettings, DeleteBook };
  std::vector<std::string> rows;
  std::vector<Action> actions;
  rows.reserve(5);
  actions.reserve(5);
  if (section_ && !section_->toc().empty()) {
    rows.push_back(tr(STR_SELECT_CHAPTER));
    actions.push_back(Action::Chapters);
  }
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

  menuPopup_.show(StrId::STR_BOOK_MENU, rows, 0, [this, actions = std::move(actions)](const int idx) {
    if (idx < 0 || idx >= static_cast<int>(actions.size())) return;
    switch (actions[idx]) {
      case Action::Chapters:
        openTocPopup();
        break;
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

bool MarkdownReaderActivity::runBoundAction(const uint8_t function) {
  if (menuPopup_.isActive()) return true;
  if (function == CrossPointSettings::LP_MENU_PAGE_PREV || function == CrossPointSettings::LP_MENU_PAGE_NEXT) {
    pageTurn(function == CrossPointSettings::LP_MENU_PAGE_NEXT);
    return true;
  }
  if (function == CrossPointSettings::LP_MENU_NEXT_CHAPTER || function == CrossPointSettings::LP_MENU_PREV_CHAPTER) {
    jumpToAdjacentHeading(function == CrossPointSettings::LP_MENU_NEXT_CHAPTER);
    return true;
  }
  if (function == CrossPointSettings::LP_MENU_SELECT_CHAPTER) {
    openTocPopup();
    return true;
  }
  if (function == CrossPointSettings::LP_MENU_OPEN_READER_MENU) {
    openMenuPopup();
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
    quietBuild_ = true;
    pagesUntilFullRefresh_ = 0;
    relayoutPreservingPercent();
    return true;
  }
  switch (simple_reader_shortcut::resolve(function, /*supportsStatusBarToggle=*/true)) {
    case simple_reader_shortcut::Action::ToggleStatusBar:
      prefs_.statusBarEnabled = prefs_.statusBarEnabled ? 0 : 1;
      prefsCustom_ = true;
      writeReaderOverride(prefs_);
      quietBuild_ = true;
      pagesUntilFullRefresh_ = 0;
      relayoutPreservingPercent();
      return true;
    case simple_reader_shortcut::Action::WallpaperHold:
      ReaderUtils::toggleWallpaperHold(renderer, pagesUntilFullRefresh_);
      requestUpdate();
      return true;
    case simple_reader_shortcut::Action::None:
      break;
  }
  return false;
}
