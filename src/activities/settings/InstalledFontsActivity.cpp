#include "InstalledFontsActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <NearbyFileGroup.h>
#include <NearbyFileRules.h>

#include <cstdio>
#include <memory>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "activities/network/NearbyFileTransferActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* LOG_TAG = "FONTS";

}  // namespace

InstalledFontsActivity::InstalledFontsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("InstalledFonts", renderer, mappedInput), fontInstaller(sdFontSystem.registry()) {}

void InstalledFontsActivity::onEnter() {
  loadFamilies();
  UiListActivity::onEnter();
}

void InstalledFontsActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
  labels.clear();
  subtitles.clear();
}

int InstalledFontsActivity::listCount() const {
  return view == View::Actions ? static_cast<int>(Action::Count) : itemCount();
}

void InstalledFontsActivity::showView(const View next) {
  view = next;
  // Each view owns its own selection: entering the actions starts on Send, and
  // coming back lands on the family that was picked.
  nav.reset(next == View::Actions ? 0 : selectedIndex);
  requestUpdate();
}

void InstalledFontsActivity::loadFamilies() {
  families.clear();

  sdFontSystem.refreshIfDirty();
  const std::string activeFamily = SETTINGS.sdFontFamilyName;
  const auto& installed = sdFontSystem.registry().getFamilies();
  families.reserve(installed.size());

  for (const auto& info : installed) {
    Family family;
    family.name = info.name;
    family.sizeCount = static_cast<uint8_t>(info.files.size());
    family.inUse = !activeFamily.empty() && activeFamily == info.name;
    family.facePaths.reserve(info.files.size());
    // A family whose folder name the offer format cannot carry stays listed and
    // deletable; it simply cannot be sent.
    family.sendable =
        !nearby_file::sanitizeFontFolder(std::string(nearby_file::FONT_FOLDER_ROOT) + "/" + info.name).empty() &&
        info.files.size() <= nearby_file::MAX_GROUP_FILES;

    // The registry knows the faces but never their size on the card, so each one
    // is opened here. It is a handful of files per family and the screen is
    // already waiting on the card scan.
    for (const auto& face : info.files) {
      family.facePaths.push_back(face.path);
      HalFile file;
      // No file.close(): DESTRUCTOR_CLOSES_FILE=1 closes it at the end of the
      // iteration.
      if (Storage.openFileForRead(LOG_TAG, face.path, file) && file.isOpen()) family.totalBytes += file.fileSize64();
    }
    families.push_back(std::move(family));
  }

  if (selectedIndex >= itemCount()) selectedIndex = 0;
}

int InstalledFontsActivity::itemCount() const { return static_cast<int>(families.size()); }

const InstalledFontsActivity::Family* InstalledFontsActivity::selectedFamily() const {
  if (selectedIndex < 0 || selectedIndex >= itemCount()) return nullptr;
  return &families[selectedIndex];
}

std::string InstalledFontsActivity::sizeLine(const Family& family) const {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), tr(STR_FONT_SIZES_FORMAT), static_cast<unsigned>(family.sizeCount),
                static_cast<unsigned>((family.totalBytes + 1023) / 1024));
  return buffer;
}

void InstalledFontsActivity::promptDelete() {
  const Family* family = selectedFamily();
  if (family == nullptr) return;

  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE), family->name),
                         [this](const ActivityResult& result) { onDeleteConfirmed(result); });
}

void InstalledFontsActivity::onDeleteConfirmed(const ActivityResult& result) {
  showView(View::Families);
  if (result.isCancelled) {
    requestUpdate();
    return;
  }

  const Family* family = selectedFamily();
  if (family == nullptr) return;

  // deleteFamily clears the reader's font setting itself when the family being
  // deleted is the one in use, so the reader falls back to a built-in rather
  // than pointing at a folder that is no longer there.
  if (fontInstaller.deleteFamily(family->name.c_str()) != FontInstaller::Error::OK) {
    errorMessage = tr(STR_FONT_DELETE_FAILED);
  } else {
    errorMessage.clear();
    fontInstaller.refreshRegistry();
    SETTINGS.saveToFile();
    loadFamilies();
  }
  requestUpdate();
}

void InstalledFontsActivity::sendSelectedFamily() {
  const Family* family = selectedFamily();
  if (family == nullptr || family->facePaths.empty()) return;

  showView(View::Families);
  // Refused here rather than at the other end: a family the offer cannot name
  // would be turned away only after the reader had picked a device to send to.
  if (!family->sendable) {
    errorMessage = tr(STR_FONT_CANNOT_SEND);
    requestUpdate();
    return;
  }

  errorMessage.clear();
  startActivityForResult(NearbyFileTransferActivity::sendFontFamily(renderer, mappedInput, family->name,
                                                                    family->facePaths, family->totalBytes),
                         [this](const ActivityResult&) { requestUpdate(true); });
}

void InstalledFontsActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (view == View::Actions) {
    if (static_cast<Action>(index) == Action::Delete) {
      promptDelete();
      return;
    }
    sendSelectedFamily();
    return;
  }

  if (families.empty()) return;
  selectedIndex = index;
  showView(View::Actions);
}

void InstalledFontsActivity::onBackButton() {
  if (view == View::Actions) {
    showView(View::Families);
    return;
  }
  finish();
}

ListChrome InstalledFontsActivity::chrome() const {
  ListChrome chrome;
  // The actions view is headed by the family it is acting on, not by the screen name.
  const Family* family = selectedFamily();
  chrome.title = (view == View::Actions && family != nullptr) ? family->name.c_str() : tr(STR_INSTALLED_FONTS);
  // With no fonts installed there is nothing to open and nothing to move between.
  const bool hasRows = listCount() > 0;
  chrome.confirmHint = hasRows ? tr(STR_SELECT) : "";
  chrome.thirdHint = hasRows ? tr(STR_DIR_UP) : "";
  chrome.fourthHint = hasRows ? tr(STR_DIR_DOWN) : "";
  if (!errorMessage.empty()) chrome.footnote = errorMessage.c_str();
  return chrome;
}

void InstalledFontsActivity::buildScreen(UiScreen& screen) {
  if (families.empty()) {
    screen.centeredText(tr(STR_NO_INSTALLED_FONTS));
    return;
  }

  const int count = listCount();
  labels.assign(static_cast<size_t>(count), std::string());
  subtitles.assign(static_cast<size_t>(count), std::string());
  rows.assign(static_cast<size_t>(count), fui::ListItem{});

  for (int i = 0; i < count; ++i) {
    if (view == View::Actions) {
      labels[i] = static_cast<Action>(i) == Action::Delete ? tr(STR_DELETE) : tr(STR_SEND_FONT);
    } else {
      labels[i] = families[i].name;
      subtitles[i] = sizeLine(families[i]);
      rows[i].subtitle = subtitles[i].c_str();
      rows[i].value = families[i].inUse ? tr(STR_SELECTED) : nullptr;
    }
    rows[i].label = labels[i].c_str();
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  syncListViewport(screen, props, view == View::Families);
  screen.list(props);
}
