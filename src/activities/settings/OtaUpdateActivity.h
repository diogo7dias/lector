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
  // Which step the FAILED screen would repeat. A check that never found a
  // release has nothing to install, so its Retry has to run the check again;
  // a download that died mid-image resumes the install it already started.
  enum class FailedStep { CHECK, INSTALL };
  FailedStep failedStep = FailedStep::CHECK;
  // Attempts the reader has asked for by hand. Unbounded on purpose: the
  // bounded retry lives inside one install (OtaRetryPolicy), and a reader who
  // keeps pressing Retry after moving closer to the router must not be told no.
  // Only ever shown, never used as a limit.
  unsigned manualRetries = 0;
  std::string retryLine;
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
  // Runs the check, then the install, from the FAILED screen. The Wi-Fi link is
  // still up (onExit is what tears it down), so a retry costs only the step
  // that failed.
  void retryFailedStep();
  // Fills failedDetail/failedExtra/failedHint and moves to FAILED. One place,
  // so the check path and the install path cannot describe the same error
  // differently or forget to record which step to repeat.
  void enterFailed(OtaUpdater::OtaUpdaterError error, FailedStep step);

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
