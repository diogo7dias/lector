#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "WifiSession.h"
#include "activities/UiStatusActivity.h"

struct WifiCredential;

// Structure to hold WiFi network information
struct WifiNetworkInfo {
  std::string ssid;
  int32_t rssi;
  bool isEncrypted;
  bool hasSavedPassword;             // Whether we have saved credentials for this network
  bool isHiddenPlaceholder = false;  // Synthetic "Add hidden network..." list entry
};

// WiFi selection states
enum class WifiSelectionState {
  AUTO_CONNECTING,    // Trying to connect to the last known network
  SCANNING,           // Scanning for networks
  NETWORK_LIST,       // Displaying available networks
  HIDDEN_SSID_ENTRY,  // Entering SSID for a hidden network
  PASSWORD_ENTRY,     // Entering password for selected network
  CONNECTING,         // Attempting to connect
  CONNECTED,          // Successfully connected
  SAVE_PROMPT,        // Asking user if they want to save the password
  CONNECTION_FAILED,  // Connection failed
  FORGET_PROMPT       // Asking user if they want to forget the network
};

/**
 * WifiSelectionActivity is responsible for scanning WiFi APs and connecting to them.
 * It will:
 * - Enter scanning mode on entry
 * - List available WiFi networks
 * - Allow selection and launch KeyboardEntryActivity for password if needed
 * - Save the password if requested
 * - Call onComplete callback when connected or cancelled
 *
 * The onComplete callback receives true if connected successfully, false if cancelled.
 */
class WifiSelectionActivity final : public UiStatusActivity {
  WifiSelectionState state = WifiSelectionState::SCANNING;
  std::vector<WifiNetworkInfo> networks;
  // The rows and the strings behind them: statusView() only hands out pointers,
  // so both outlive it. Rebuilt with the network view.
  std::vector<freeink::ui::ListItem> rows;
  std::vector<std::string> rowLabels;
  std::vector<std::string> rowValues;
  void refreshRows();
  // Number of real (scanned) networks, excluding the synthetic hidden-network entry
  size_t realNetworkCount = 0;

  // Which network the prompts and the join are about
  std::string selectedSSID;

  // Connection result
  std::string connectedIP;
  std::string connectionError;

  // Password to potentially save (from keyboard or saved credentials)
  std::string enteredPassword;

  // Cached MAC address string for display
  std::string cachedMacAddress;
  // Header count and the two message lines the connect screens show, held for
  // the same reason as the rows.
  std::string networkCountLine;
  std::string ssidLine;
  std::string ipLine;
  void refreshHeaderCount();
  void refreshConnectionLines();

  // Whether to attempt auto-connect on entry
  const bool allowAutoConnect;

  // Which network to try, how long to wait, what to remember. Everything this
  // screen decides lives here; the activity only works the radio and the panel.
  wifi_session::WifiSession session;

  // A scan was started and its result has not been handed to the session yet.
  bool scanPending = false;
  // A join was started and its outcome has not been handed to the session yet.
  bool joinPending = false;


  /** Hands the session everything the radio has to say, then runs what it asks for. */
  void pumpSession();
  void runAction(const wifi_session::Action& action);
  void pollRadio();
  void rebuildNetworkView();
  void appendHiddenNetworkEntry();
  void syncStateFromSession();
  /** True while a keyboard or a prompt owns the screen and the session must wait. */
  bool promptIsOpen() const;

  void startWifiScan();
  void beginJoin(const std::string& ssid, const std::string& password);
  /** Left forgets a saved network, Right rescans; true when one of them acted. */
  bool handleListSideButtons();
  void promptHiddenSsid();
  void promptPasswordEntry();
  std::string getSignalStrengthIndicator(int32_t rssi) const;

  void onComplete(bool connected);

 public:
  explicit WifiSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool autoConnect = true)
      : UiStatusActivity("WifiSelection", renderer, mappedInput), allowAutoConnect(autoConnect) {}
  void onEnter() override;
  void onExit() override;

 protected:
  StatusView statusView() const override;
  bool handleCustomInput() override;
  void onListActivated(int index) override;
  void onChoiceActivated(int index) override;
  void onBackButton() override;
  void onConfirmButton() override;
};
