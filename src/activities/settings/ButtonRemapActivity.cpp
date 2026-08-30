#include "ButtonRemapActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// UI steps correspond to logical roles in order: Back, Confirm, Left, Right.
constexpr uint8_t kRoleCount = 4;
// Marker used when a role has not been assigned yet.
constexpr uint8_t kUnassigned = 0xFF;
// Duration to show temporary error text when reassigning a button.
constexpr unsigned long kErrorDisplayMs = 1500;
}  // namespace

void ButtonRemapActivity::onEnter() {
  UiListActivity::onEnter();

  // Start with all roles unassigned to avoid duplicate blocking.
  currentStep = 0;
  tempMapping[0] = kUnassigned;
  tempMapping[1] = kUnassigned;
  tempMapping[2] = kUnassigned;
  tempMapping[3] = kUnassigned;
  errorMessage.clear();
  errorUntil = 0;
  requestUpdate();
}

void ButtonRemapActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
}

int ButtonRemapActivity::listCount() const { return kRoleCount; }

bool ButtonRemapActivity::handleCustomInput() {
  // Clear any temporary warning after its timeout.
  if (errorUntil > 0 && millis() > errorUntil) {
    errorMessage.clear();
    errorUntil = 0;
    requestUpdate();
    return true;
  }

  // Side buttons:
  // - Up: reset mapping to defaults and exit.
  // - Down: cancel without saving.
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    // Persist default mapping immediately so the user can recover quickly.
    SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
    SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
    SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
    SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
    SETTINGS.saveToFile();
    finish();
    return true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    // Exit without changing settings.
    finish();
    return true;
  }

  {
    // Make sure UI done rendering before accepting another assignment.
    // This avoids rapid double-presses that can advance the step without a visible redraw.
    RenderLock lock(*this);

    // Wait for a front button press to assign to the current role.
    const int pressedButton = mappedInput.getPressedFrontButton();
    if (pressedButton < 0) {
      return true;
    }

    // Update temporary mapping and advance the remap step.
    // Only accept the press if this hardware button isn't already assigned elsewhere.
    if (!validateUnassigned(static_cast<uint8_t>(pressedButton))) {
      requestUpdate();
      return true;
    }
    tempMapping[currentStep] = static_cast<uint8_t>(pressedButton);
    currentStep++;

    if (currentStep >= kRoleCount) {
      // All roles assigned; save to settings and exit.
      applyTempMapping();
      SETTINGS.saveToFile();
      finish();
      return true;
    }

    requestUpdate();
  }
  // Every pass belongs to the remap: the base must never move a selection with a
  // button whose meaning is exactly what this screen is asking about.
  return true;
}

ListChrome ButtonRemapActivity::chrome() const {
  const auto labelForHardware = [this](const uint8_t hardwareIndex) -> const char* {
    for (uint8_t i = 0; i < kRoleCount; i++) {
      if (tempMapping[i] == hardwareIndex) return getRoleName(i);
    }
    return "-";
  };

  ListChrome chrome;
  chrome.title = tr(STR_REMAP_FRONT_BUTTONS);
  chrome.subHeader = tr(STR_REMAP_PROMPT);
  // The duplicate warning sits under the rows rather than over them, so the list
  // the user is answering stays readable while it is up.
  if (!errorMessage.empty()) chrome.note = errorMessage.c_str();
  chrome.footnotes[0] = tr(STR_REMAP_RESET_HINT);
  chrome.footnotes[1] = tr(STR_REMAP_CANCEL_HINT);
  // The hints are a live preview of the mapping being built, in the on-device
  // front button order: Back, Confirm, Left, Right.
  chrome.backHint = labelForHardware(CrossPointSettings::FRONT_HW_BACK);
  chrome.confirmHint = labelForHardware(CrossPointSettings::FRONT_HW_CONFIRM);
  chrome.thirdHint = labelForHardware(CrossPointSettings::FRONT_HW_LEFT);
  chrome.fourthHint = labelForHardware(CrossPointSettings::FRONT_HW_RIGHT);
  return chrome;
}

void ButtonRemapActivity::buildScreen(UiScreen& screen) {
  rows.assign(kRoleCount, freeink::ui::ListItem{});
  for (uint8_t i = 0; i < kRoleCount; i++) {
    rows[i].label = getRoleName(i);
    rows[i].value = tempMapping[i] == kUnassigned ? tr(STR_UNASSIGNED) : getHardwareName(tempMapping[i]);
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  freeink::ui::ListProps props{};
  props.items = rows.data();
  props.count = kRoleCount;
  props.action = ACTION_ROW;
  // The step being asked about is the selection; nothing else can move it.
  nav.selected = currentStep;
  nav.follow(kRoleCount);
  syncListViewport(screen, props);
  screen.list(props);
}

void ButtonRemapActivity::applyTempMapping() {
  // Commit temporary mapping into settings (logical role -> hardware).
  SETTINGS.frontButtonBack = tempMapping[0];
  SETTINGS.frontButtonConfirm = tempMapping[1];
  SETTINGS.frontButtonLeft = tempMapping[2];
  SETTINGS.frontButtonRight = tempMapping[3];
}

bool ButtonRemapActivity::validateUnassigned(const uint8_t pressedButton) {
  // Block reusing a hardware button already assigned to another role.
  for (uint8_t i = 0; i < kRoleCount; i++) {
    if (tempMapping[i] == pressedButton && i != currentStep) {
      errorMessage = tr(STR_ALREADY_ASSIGNED);
      errorUntil = millis() + kErrorDisplayMs;
      return false;
    }
  }
  return true;
}

const char* ButtonRemapActivity::getRoleName(const uint8_t roleIndex) const {
  switch (roleIndex) {
    case 0:
      return tr(STR_BACK);
    case 1:
      return tr(STR_CONFIRM);
    case 2:
      return tr(STR_DIR_LEFT);
    case 3:
    default:
      return tr(STR_DIR_RIGHT);
  }
}

const char* ButtonRemapActivity::getHardwareName(const uint8_t buttonIndex) const {
  switch (buttonIndex) {
    case CrossPointSettings::FRONT_HW_BACK:
      return tr(STR_HW_BACK_LABEL);
    case CrossPointSettings::FRONT_HW_CONFIRM:
      return tr(STR_HW_CONFIRM_LABEL);
    case CrossPointSettings::FRONT_HW_LEFT:
      return tr(STR_HW_LEFT_LABEL);
    case CrossPointSettings::FRONT_HW_RIGHT:
      return tr(STR_HW_RIGHT_LABEL);
    default:
      return "Unknown";
  }
}
