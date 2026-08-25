#pragma once

#include <vector>

#include "activities/UiListActivity.h"

enum class NetworkMode { JOIN_NETWORK, CONNECT_CALIBRE, CREATE_HOTSPOT, NEARBY_READER };

/**
 * NetworkModeSelectionActivity presents the user with a choice:
 * - "Join a Network" - Connect to an existing WiFi network (STA mode)
 * - "Connect to Calibre" - Use Calibre wireless device transfers
 * - "Create Hotspot" - Create an Access Point that others can connect to (AP mode)
 * - "Nearby Reader" - Receive a file straight from another reader over ESP-NOW,
 *   with no network involved at all
 *
 * The chosen mode is returned as a NetworkModeResult; Back cancels.
 */
class NetworkModeSelectionActivity final : public UiListActivity {
 public:
  explicit NetworkModeSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("NetworkModeSelection", renderer, mappedInput) {}

  void onExit() override;

  void onModeSelected(NetworkMode mode);
  void onCancel();

 protected:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override { onCancel(); }
  const char* headerTitle() const override;

 private:
  std::vector<freeink::ui::ListItem> rows;
};
