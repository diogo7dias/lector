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

namespace {

constexpr const char* LOG_TAG = "FONTS";

}  // namespace

InstalledFontsActivity::InstalledFontsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("InstalledFonts", renderer, mappedInput), fontInstaller(sdFontSystem.registry()) {}

void InstalledFontsActivity::onEnter() {
  Activity::onEnter();
  loadFamilies();
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
  view = View::Families;
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

  view = View::Families;
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

void InstalledFontsActivity::loopFamilies() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (families.empty()) return;

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    selectedAction = Action::Send;
    view = View::Actions;
    requestUpdate();
    return;
  }

  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);
  const int total = itemCount();

  buttonNavigator.onNextStep([this, total] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, total);
    requestUpdate();
  });
  buttonNavigator.onPreviousStep([this, total] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, total);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, total, pageItems] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, total, pageItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, total, pageItems] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, total, pageItems);
    requestUpdate();
  });
}

void InstalledFontsActivity::loopActions() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    view = View::Families;
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedAction == Action::Delete) {
      promptDelete();
    } else {
      sendSelectedFamily();
    }
    return;
  }

  constexpr int actionCount = static_cast<int>(Action::Count);
  buttonNavigator.onNextStep([this] {
    selectedAction = static_cast<Action>(ButtonNavigator::nextIndex(static_cast<int>(selectedAction), actionCount));
    requestUpdate();
  });
  buttonNavigator.onPreviousStep([this] {
    selectedAction = static_cast<Action>(ButtonNavigator::previousIndex(static_cast<int>(selectedAction), actionCount));
    requestUpdate();
  });
}

void InstalledFontsActivity::loop() {
  if (view == View::Actions) {
    loopActions();
    return;
  }
  loopFamilies();
}

void InstalledFontsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_INSTALLED_FONTS));

  if (families.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, (pageHeight - lineHeight) / 2, tr(STR_NO_INSTALLED_FONTS));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (view == View::Actions) {
    const Family* family = selectedFamily();
    const std::string title = family != nullptr ? family->name : std::string();
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop, title.c_str(), true, EpdFontFamily::REGULAR);

    GUI.drawList(
        renderer, Rect{0, contentTop + lineHeight * 2, pageWidth, contentHeight - lineHeight * 2},
        static_cast<int>(Action::Count), static_cast<int>(selectedAction),
        [](const int index) -> std::string {
          return static_cast<Action>(index) == Action::Delete ? tr(STR_DELETE) : tr(STR_SEND_FONT);
        },
        nullptr, nullptr, nullptr, true);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, itemCount(), selectedIndex,
      [this](const int index) -> std::string { return families[index].name; },
      [this](const int index) -> std::string { return sizeLine(families[index]); }, nullptr,
      [this](const int index) -> std::string { return families[index].inUse ? tr(STR_SELECTED) : ""; }, true);

  if (!errorMessage.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight - metrics.buttonHintsHeight - lineHeight, errorMessage.c_str());
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
