#pragma once

#include <memory>
#include <string>

#include "activities/UiStatusActivity.h"
#include "network/OtaUpdater.h"

class OtaUpdateActivity : public UiStatusActivity {
  enum State {
    WIFI_SELECTION,
    CHECKING_FOR_UPDATE,
    WAITING_CONFIRMATION,
    UPDATE_IN_PROGRESS,
    NO_UPDATE,
    FAILED,
    FINISHED,
    SHUTTING_DOWN
  };

  // Out of range on purpose: the first progress callback must always be let
  // through, and every real percent is 0 to 100.
  static constexpr unsigned int UNINITIALIZED_PERCENTAGE = 111;

  State state = WIFI_SELECTION;
  // Install whatever the update server offers, whatever its version and
  // whichever firmware it is. Set from the start by the "Install Other
  // Firmware" entry, or from the "no update" screen. This is the only way off
  // lector on a device whose USB flashing the vendor locked, so it must never
  // be gated on the offered version being newer.
  bool allowAnyVersion = false;
  // True while the framebuffer is lent to wolfSSL: nothing may draw, including
  // the progress callback, until the transfer ends.
  volatile bool drawingSuspended = false;
  unsigned int lastUpdaterPercentage = UNINITIALIZED_PERCENTAGE;
  OtaUpdater updater;
  // Optional detail line shown under the generic "Update failed" heading.
  // Points into the i18n string table (flash-resident, so no lifetime concern);
  // nullptr means no extra detail.
  const char* failedDetail = nullptr;
  std::string failedExtra;
  std::string failedHint;
  // "Current version: x" / "New version: y" / "4096 / 1200000", all built when
  // the state that shows them is entered or when the transfer moves.
  std::string currentVersionLine;
  std::string newVersionLine;
  std::string bytesLine;

  void onWifiSelectionComplete(bool success);
  // Maps an updater error onto the optional detail line under "Update failed".
  static const char* detailFor(OtaUpdater::OtaUpdaterError error);
  void runUpdateInstall();

 public:
  explicit OtaUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             const bool installOtherFirmware = false)
      : UiStatusActivity("OtaUpdate", renderer, mappedInput), allowAnyVersion(installOtherFirmware), updater() {}
  void onEnter() override;
  void onExit() override;
  bool preventAutoSleep() override { return state == CHECKING_FOR_UPDATE || state == UPDATE_IN_PROGRESS; }
  bool skipLoopDelay() override { return true; }  // Prevent power-saving mode

 protected:
  StatusView statusView() const override;
  bool handleCustomInput() override;
  void onConfirmButton() override;
  void onBackButton() override;
};
