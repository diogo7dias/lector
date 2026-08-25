#include "ButtonBindingsActivity.h"

#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "SettingsList.h"
#include "SettingsListNav.h"
#include "components/UITheme.h"
#include "util/BoundActionScope.h"
#include "util/BoundMenuLabels.h"

namespace fui = freeink::ui;

namespace {

// The bindings view, in the order it is drawn: each group is a heading followed by the
// three gestures. In book comes first because that is where a reader spends its life.
constexpr uint8_t GESTURES[] = {CrossPointSettings::BOUND_SINGLE, CrossPointSettings::BOUND_DOUBLE,
                                CrossPointSettings::BOUND_HOLD};

StrId gestureLabel(const uint8_t gesture) {
  switch (gesture) {
    case CrossPointSettings::BOUND_SINGLE:
      return StrId::STR_SINGLE_CLICK;
    case CrossPointSettings::BOUND_DOUBLE:
      return StrId::STR_DOUBLE_CLICK;
    default:
      return StrId::STR_HOLD_GESTURE;
  }
}

}  // namespace

ButtonBindingsActivity::ButtonBindingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("ButtonBindings", renderer, mappedInput) {}

void ButtonBindingsActivity::onEnter() {
  loadButtons();
  UiListActivity::onEnter();
}

void ButtonBindingsActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
  labels.clear();
  values.clear();
}

void ButtonBindingsActivity::loadButtons() {
  buttons.clear();
  // The two side keys are on every board. The Home key is capacitive and only some
  // boards have one; listing it elsewhere would offer bindings nothing can fire.
  buttons.push_back(CrossPointSettings::BOUND_BTN_LEFT);
  buttons.push_back(CrossPointSettings::BOUND_BTN_RIGHT);
  if (gpio.hasHomeKey()) buttons.push_back(CrossPointSettings::BOUND_BTN_HOME);
}

void ButtonBindingsActivity::buildBindingRows() {
  bindingRows.clear();
  for (const bool inBook : {true, false}) {
    BindingRow header;
    header.isHeader = true;
    header.inBook = inBook;
    bindingRows.push_back(header);
    for (const uint8_t gesture : GESTURES) {
      BindingRow row;
      row.inBook = inBook;
      row.gesture = gesture;
      bindingRows.push_back(row);
    }
  }
}

const char* ButtonBindingsActivity::buttonLabel(const uint8_t button) const {
  switch (button) {
    case CrossPointSettings::BOUND_BTN_LEFT:
      return tr(STR_BTN_LEFT);
    case CrossPointSettings::BOUND_BTN_RIGHT:
      return tr(STR_BTN_RIGHT);
    default:
      return tr(STR_BTN_HOME);
  }
}

uint8_t* ButtonBindingsActivity::bindingFor(const size_t row) const {
  if (row >= bindingRows.size() || bindingRows[row].isHeader) return nullptr;
  if (selectedButton < 0 || selectedButton >= static_cast<int>(buttons.size())) return nullptr;
  return SETTINGS.buttonBinding(bindingRows[row].inBook, buttons[selectedButton], bindingRows[row].gesture);
}

int ButtonBindingsActivity::listCount() const {
  return view == View::Buttons ? static_cast<int>(buttons.size()) : static_cast<int>(bindingRows.size());
}

void ButtonBindingsActivity::showView(const View next) {
  view = next;
  if (next == View::Bindings) buildBindingRows();
  // Each view owns its own selection: the bindings open on the first binding (never on
  // the heading above it), and coming back lands on the key that was picked.
  nav.reset(next == View::Bindings ? 1 : selectedButton);
  requestUpdate();
}

void ButtonBindingsActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (view == View::Buttons) {
    if (index < 0 || index >= static_cast<int>(buttons.size())) return;
    selectedButton = index;
    showView(View::Bindings);
    return;
  }
  const size_t row = static_cast<size_t>(index);
  if (bindingFor(row) == nullptr) return;  // a heading: nothing to edit
  openActionPicker(row);
}

void ButtonBindingsActivity::openActionPicker(const size_t row) {
  const uint8_t* binding = bindingFor(row);
  if (binding == nullptr) return;
  const bool inBook = bindingRows[row].inBook;
  const auto retired = retiredBoundFunctions();

  std::vector<std::string> options;
  std::vector<bool> disabledRows;
  pickerFunctions.clear();
  int currentIndex = 0;
  for (uint8_t function = 0; function < CrossPointSettings::LONG_PRESS_MENU_FUNCTION_COUNT; ++function) {
    if (std::find(retired.begin(), retired.end(), function) != retired.end()) continue;
    // Greyed rather than hidden outside a book: the two groups then list the same
    // actions in the same order, so the one that is missing reads as unavailable here
    // rather than as absent from the firmware.
    const bool unavailable = !inBook && !bound_action::allowedOutsideBook(function);
    options.push_back(std::string(unavailable ? "[X] " : "    ") + I18N.get(boundMenuActionLabel(function)));
    disabledRows.push_back(unavailable);
    if (function == *binding) currentIndex = static_cast<int>(pickerFunctions.size());
    pickerFunctions.push_back(function);
  }
  if (pickerFunctions.empty()) return;

  pickerRow = row;
  actionPopup.showWithDisabled(gestureLabel(bindingRows[row].gesture), options, disabledRows, currentIndex, true,
                               [this](const int choice) {
                                 if (choice < 0 || choice >= static_cast<int>(pickerFunctions.size())) return;
                                 uint8_t* target = bindingFor(pickerRow);
                                 if (target == nullptr) return;
                                 *target = pickerFunctions[choice];
                                 SETTINGS.saveToFile();
                                 requestUpdate();
                               });
  requestUpdate();
}

void ButtonBindingsActivity::navigateButtons() {
  if (view == View::Buttons) {
    UiListActivity::navigateButtons();
    return;
  }
  // Headings are drawn but never landed on, so a step past one keeps going.
  std::vector<bool> headerFlags;
  headerFlags.reserve(bindingRows.size());
  for (const auto& row : bindingRows) headerFlags.push_back(row.isHeader);
  buttonNavigator.onNextRelease(
      [this, &headerFlags] { moveSelectionTo(settings_nav::nextRow(nav.selected, headerFlags, true)); });
  buttonNavigator.onPreviousRelease(
      [this, &headerFlags] { moveSelectionTo(settings_nav::nextRow(nav.selected, headerFlags, false)); });
  buttonNavigator.onNextContinuous(
      [this, &headerFlags] { moveSelectionTo(settings_nav::nextSection(nav.selected, headerFlags, true)); });
  buttonNavigator.onPreviousContinuous(
      [this, &headerFlags] { moveSelectionTo(settings_nav::nextSection(nav.selected, headerFlags, false)); });
}

void ButtonBindingsActivity::onBackButton() {
  if (view == View::Bindings) {
    showView(View::Buttons);
    return;
  }
  finish();
}

bool ButtonBindingsActivity::handleCustomInput() {
  if (actionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    // The pop-up acts on button press; if that input closed it, the trailing release
    // must be swallowed below (Back would leave the screen, Confirm would reopen it).
    popupClosing = !actionPopup.isActive();
    return true;
  }
  if (popupClosing) {
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      return true;  // closing press still held
    }
    popupClosing = false;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      return true;  // swallow the release that closed the pop-up
    }
  }
  return false;
}

bool ButtonBindingsActivity::drawOverlay() { return actionPopup.processRender(renderer, mappedInput); }

void ButtonBindingsActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const char* title = view == View::Bindings && selectedButton < static_cast<int>(buttons.size())
                          ? buttonLabel(buttons[selectedButton])
                          : tr(STR_BUTTONS);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, title);
}

void ButtonBindingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing), 0,
                  static_cast<int16_t>(metrics.buttonHintsHeight + metrics.verticalSpacing), 0});

  const int count = listCount();
  if (count <= 0) return;
  labels.assign(static_cast<size_t>(count), std::string());
  values.assign(static_cast<size_t>(count), std::string());
  rows.assign(static_cast<size_t>(count), fui::ListItem{});

  for (int i = 0; i < count; ++i) {
    const size_t row = static_cast<size_t>(i);
    if (view == View::Buttons) {
      labels[row] = buttonLabel(buttons[row]);
    } else if (bindingRows[row].isHeader) {
      labels[row] = I18N.get(bindingRows[row].inBook ? StrId::STR_IN_BOOK : StrId::STR_OUTSIDE_BOOK);
      rows[row].isHeader = true;
    } else {
      labels[row] = I18N.get(gestureLabel(bindingRows[row].gesture));
      const uint8_t* binding = bindingFor(row);
      if (binding != nullptr) {
        values[row] = I18N.get(boundMenuActionLabel(*binding));
        rows[row].value = values[row].c_str();
      }
    }
    rows[row].label = labels[row].c_str();
    rows[row].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  syncListViewport(screen, props);
  screen.list(props);
}
