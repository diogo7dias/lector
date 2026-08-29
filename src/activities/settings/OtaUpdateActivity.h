#pragma once

#include <memory>

#include "activities/Activity.h"
#include "network/OtaUpdater.h"

class OtaUpdateActivity : public Activity {
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

  // Can't initialize this to 0 or the first render doesn't happen
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

  void onWifiSelectionComplete(bool success);
  // Maps an updater error onto the optional detail line under "Update failed".
  static const char* detailFor(OtaUpdater::OtaUpdaterError error);
  void runUpdateInstall();

 public:
  explicit OtaUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             const bool installOtherFirmware = false)
      : Activity("OtaUpdate", renderer, mappedInput), allowAnyVersion(installOtherFirmware), updater() {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CHECKING_FOR_UPDATE || state == UPDATE_IN_PROGRESS; }
  bool skipLoopDelay() override { return true; }  // Prevent power-saving mode
};
