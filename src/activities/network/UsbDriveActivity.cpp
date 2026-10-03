#if FREEINK_CAP_USB_MSC

#include "UsbDriveActivity.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <PerfLog.h>

#include "Diagnostics.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "platform/UsbSerialJtagHandoff.h"

void UsbDriveActivity::onEnter() {
  UiStatusActivity::onEnter();
  // The instructions go up before the card leaves the firmware's hands.
  requestUpdateAndWait();
  // Last writes the firmware owes the card: once it is lent, HalStorage refuses them.
  PerfLog::flush();
  diag::flushIfPending();
  persistAntiGhostBudget();
  if (Storage.beginUsbDrive()) {
    enterPhase(Phase::Waiting);
  } else {
    LOG_ERR("USB", "Unable to start USB Drive");
    enterPhase(Phase::StartFailed);
  }
}

void UsbDriveActivity::enterPhase(const Phase next) {
  if (next == phase) return;
  phase = next;
  phaseStartedAt = millis();
  requestUpdate();
}

bool UsbDriveActivity::handleCustomInput() {
  if (phase != Phase::StartFailed) {
    switch (Storage.usbDriveState()) {
      case UsbDriveState::WaitingForHost:
        enterPhase(Phase::Waiting);
        break;
      case UsbDriveState::Connected:
        enterPhase(Phase::Connected);
        break;
      case UsbDriveState::IoError:
        enterPhase(Phase::IoError);
        break;
      default:
        // Ejected, cable out, or MSC gone: the card is ours again, but only a reboot remounts it.
        restartToHome();
        return true;
    }
  }

  const unsigned long elapsed = millis() - phaseStartedAt;
  switch (phase) {
    case Phase::StartFailed:
      if (elapsed >= START_FAILURE_TIMEOUT_MS) restartToHome();
      break;
    case Phase::Waiting:
      if (elapsed >= HOST_WAIT_TIMEOUT_MS) {
        LOG_INF("USB", "No computer connected; leaving USB Drive");
        restartToHome();
      }
      break;
    case Phase::IoError:
      // The computer already saw a failed block request: drop off the bus rather than
      // wait for the cable to come out, and reboot anyway if the drop never lands.
      if (!disconnectRequested) {
        disconnectRequested = true;
        phaseStartedAt = millis();
        LOG_ERR("USB", "USB Drive I/O error; disconnecting the computer");
        if (!Storage.disconnectUsbDriveHost()) LOG_ERR("USB", "USB Drive disconnect request failed");
      } else if (elapsed >= FORCED_DISCONNECT_TIMEOUT_MS) {
        restartToHome();
      }
      return true;
    case Phase::Preparing:
    case Phase::Connected:
      break;
  }

  if (canLeave() && (mappedInput.wasPressed(MappedInputManager::Button::Power) || mappedInput.wasHomeGesture())) {
    restartToHome();
    return true;
  }
  return false;
}

void UsbDriveActivity::onBackButton() {
  if (canLeave()) restartToHome();
}

UiStatusActivity::StatusView UsbDriveActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_USB_DRIVE);
  switch (phase) {
    case Phase::Preparing:
      view.lines = {tr(STR_USB_DRIVE_PREPARING), tr(STR_USB_DRIVE_EJECT_HINT), nullptr, nullptr};
      break;
    case Phase::StartFailed:
      view.lines = {tr(STR_USB_DRIVE_START_ERROR), nullptr, nullptr, nullptr};
      break;
    case Phase::Waiting:
      view.lines = {tr(STR_USB_DRIVE_WAITING), nullptr, nullptr, nullptr};
      break;
    case Phase::Connected:
      view.lines = {tr(STR_USB_DRIVE_CONNECTED), tr(STR_USB_DRIVE_CONNECT_DELAY), tr(STR_USB_DRIVE_EJECT_HINT),
                    nullptr};
      break;
    case Phase::IoError:
      view.lines = {tr(STR_USB_DRIVE_ERROR), nullptr, nullptr, nullptr};
      break;
  }
  if (!canLeave()) view.backHint = "";
  return view;
}

void UsbDriveActivity::restartToHome() {
  // Grace before teardown: an eject is noticed from the start/stop callback, before
  // TinyUSB has sent the host its status reply for that command.
  delay(20);
  Storage.endUsbDrive();
  handoffUsbOtgToSerialJtag();
  silentRestart();
}

#endif  // FREEINK_CAP_USB_MSC
