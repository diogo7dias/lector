#include "SettingSelectActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void SettingSelectActivity::onEnter() {
  Activity::onEnter();

  if (options_.empty()) {
    finish();
    return;
  }
  selectedIndex_ = std::clamp(selectedIndex_, 0, static_cast<int>(options_.size()) - 1);
  originalIndex_ = std::clamp(originalIndex_, 0, static_cast<int>(options_.size()) - 1);

  requestUpdate();
}

void SettingSelectActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  const int total = static_cast<int>(options_.size());
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);

  buttonNavigator_.onNextRelease([this, total] {
    selectedIndex_ = ButtonNavigator::nextIndex(selectedIndex_, total);
    requestUpdate();
  });

  buttonNavigator_.onPreviousRelease([this, total] {
    selectedIndex_ = ButtonNavigator::previousIndex(selectedIndex_, total);
    requestUpdate();
  });

  buttonNavigator_.onNextContinuous([this, total, pageItems] {
    selectedIndex_ = ButtonNavigator::nextPageIndex(selectedIndex_, total, pageItems);
    requestUpdate();
  });

  buttonNavigator_.onPreviousContinuous([this, total, pageItems] {
    selectedIndex_ = ButtonNavigator::previousPageIndex(selectedIndex_, total, pageItems);
    requestUpdate();
  });
}

void SettingSelectActivity::handleSelection() {
  if (onSelect_) {
    onSelect_(static_cast<uint8_t>(selectedIndex_));
  }
  finish();
}

void SettingSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title_.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(options_.size()), selectedIndex_,
      [this](int index) { return options_[index]; }, nullptr, nullptr,
      [this](int index) { return index == originalIndex_ ? tr(STR_SELECTED) : ""; }, true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
