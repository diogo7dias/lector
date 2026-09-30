#pragma once

#include <functional>
#include <string>

#include "activities/UiStatusActivity.h"

/**
 * Activity for testing KOReader credentials, or — in sign-up mode — creating a
 * new account on the sync server with the entered username/password.
 * Connects to WiFi, then authenticates or registers.
 */
class KOReaderAuthActivity final : public UiStatusActivity {
 public:
  enum class Mode { AUTHENTICATE, SIGN_UP };

  explicit KOReaderAuthActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode = Mode::AUTHENTICATE)
      : UiStatusActivity(activity_name::kKOReaderAuth, renderer, mappedInput), mode(mode) {}

  void onEnter() override;
  void onExit() override;
  bool preventAutoSleep() override { return state == CONNECTING || state == AUTHENTICATING; }

 protected:
  StatusView statusView() const override;
  bool contentsLook() const override { return true; }
  void onBackButton() override;
  void onConfirmButton() override;

 private:
  enum State { WIFI_SELECTION, CONNECTING, AUTHENTICATING, SUCCESS, FAILED };

  Mode mode = Mode::AUTHENTICATE;
  State state = WIFI_SELECTION;
  std::string statusMessage;
  std::string errorMessage;

  void onWifiSelectionComplete(bool success);
  void performAuthentication();
};
