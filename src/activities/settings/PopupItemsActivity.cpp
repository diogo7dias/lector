#include "PopupItemsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <iterator>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BoundMenuLabels.h"

namespace {

constexpr int ITEM_COUNT = static_cast<int>(std::size(CrossPointSettings::POPUP_ITEM_FUNCTIONS));

}  // namespace

void PopupItemsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void PopupItemsActivity::onExit() { Activity::onExit(); }

void PopupItemsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  // A tap on a row selects and activates it, the same as every other list.
  int tappedRow = 0;
  if (mappedInput.wasRowTapped(tappedRow) && tappedRow >= 0 && tappedRow < static_cast<int>(ITEM_COUNT)) {
    selectedIndex = tappedRow;
    toggleSelected();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    toggleSelected();
    return;
  }

  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);

  buttonNavigator.onNextStep([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, ITEM_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPreviousStep([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, ITEM_COUNT);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, pageItems] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, ITEM_COUNT, pageItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, pageItems] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, ITEM_COUNT, pageItems);
    requestUpdate();
  });
}

void PopupItemsActivity::toggleSelected() {
  const uint8_t function = CrossPointSettings::POPUP_ITEM_FUNCTIONS[selectedIndex];
  const bool wantOn = !SETTINGS.isPopupItem(function);

  if (!SETTINGS.setPopupItem(function, wantOn)) {
    // The only way a toggle is refused is the cap; say so and leave the banner up. No
    // requestUpdate() here on purpose — that would repaint the list straight over it. The
    // next navigation press brings the list back, which is how every transient banner in
    // the firmware behaves on e-ink.
    GUI.drawPopup(renderer, tr(STR_POPUP_FULL));
    return;
  }

  // Ticks are rare and each one is a real user decision, so a save per toggle stays
  // well inside the SPIFFS write budget; setPopupItem() already returned false for a
  // no-op, so an unchanged mask never reaches this line.
  SETTINGS.saveToFile();
  requestUpdate();
}

void PopupItemsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto metrics = UITheme::getInstance().getMetrics();

  char counter[16];
  snprintf(counter, sizeof(counter), "%u / %u", static_cast<unsigned>(SETTINGS.popupItemCount()),
           static_cast<unsigned>(CrossPointSettings::POPUP_ITEM_MAX));
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_POPUP_ITEMS), counter);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, ITEM_COUNT, selectedIndex, [](int index) {
    const uint8_t function = CrossPointSettings::POPUP_ITEM_FUNCTIONS[index];
    // The box sits on the left, in a fixed-width column, so ticking a row never
    // shifts its label sideways.
    return std::string(SETTINGS.isPopupItem(function) ? "[x]  " : "[ ]  ") + I18N.get(boundMenuActionLabel(function));
  });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
