#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * The decision half of the WiFi selection screen: which network to try, how long
 * to wait for it, and what to remember afterwards.
 *
 * Like NearbyPositionSession beside it, this holds no radio, screen or storage
 * handles. The activity feeds it scan results, join outcomes and button
 * presses along with the current millisecond clock, then drains the actions it
 * asks for. That keeps every timeout and transition reachable from host tests,
 * where the radio is not.
 *
 * Passwords never enter this module. It names the network to join and the
 * credential to save; the activity resolves the secret from WifiCredentialStore.
 */
namespace wifi_session {

/** Longest SSID the 802.11 standard allows, plus the terminator. */
constexpr size_t SSID_BUFFER_BYTES = 33;

/** How long to wait for a join the reader asked for. */
constexpr uint32_t JOIN_TIMEOUT_MS = 15000;
/** How long to wait for a join nobody asked for, before trying the next network. */
constexpr uint32_t AUTO_JOIN_TIMEOUT_MS = 7000;
/**
 * How long to wait for scan results. The radio can return neither results nor a
 * failure, which used to leave the screen scanning forever.
 */
constexpr uint32_t SCAN_TIMEOUT_MS = 15000;

struct Network {
  char ssid[SSID_BUFFER_BYTES] = {};
  int32_t rssi = 0;
  bool isEncrypted = false;
  bool hasSavedPassword = false;
  bool isHiddenPlaceholder = false;
};

enum class State : uint8_t {
  AUTO_CONNECTING,  // Trying saved networks without asking
  SCANNING,         // Waiting on scan results
  NETWORK_LIST,     // Waiting on the reader to pick
  CONNECTING,       // Joining a network the reader picked
  CONNECTED,        // Joined
  FAILED,           // The join the reader asked for did not take
};

enum class ActionKind : uint8_t {
  START_SCAN,
  JOIN,
  DISCONNECT,
  ASK_FOR_PASSWORD,
  SAVE_CREDENTIAL,
  FORGET_CREDENTIAL,
  FINISH,
};

struct Action {
  ActionKind kind = ActionKind::START_SCAN;
  /** The network this action is about. Empty for START_SCAN and DISCONNECT. */
  std::string ssid;
  /** JOIN only: the activity supplies the stored secret rather than a typed one. */
  bool useSavedPassword = false;
  /** FINISH only: whether the screen leaves with a connection. */
  bool connected = false;
};

/** What the activity knows before the screen opens. */
struct Startup {
  bool allowAutoConnect = true;
  std::vector<std::string> savedSsids;
  std::string lastConnectedSsid;
};

class WifiSession {
 public:
  void begin(const Startup& startup, uint32_t nowMs);

  /** Feeds in one completed scan. */
  void onScanResults(const Network* found, size_t count, uint32_t nowMs);

  /** The radio could not join the network named by the last JOIN action. */
  void onJoinFailed(uint32_t nowMs);

  /** The radio reported that the scan produced nothing. */
  void onScanFailed(uint32_t nowMs);

  /** The radio joined the network named by the last JOIN action. */
  void onJoinSucceeded(uint32_t nowMs);

  /** The reader picked the network at `index` in `networks()`. */
  void selectNetwork(size_t index, uint32_t nowMs);

  /** The reader finished typing a password for the network being joined. */
  void onPasswordEntered(uint32_t nowMs);

  /** The reader backed out of the password keyboard without typing one. */
  void abandonPasswordEntry(uint32_t nowMs);

  /** The reader typed the name of a network that does not broadcast one. */
  void selectHiddenNetwork(const std::string& ssid, uint32_t nowMs);

  /** The reader answered the offer to remember the password they typed. */
  void answerSavePrompt(bool remember, uint32_t nowMs);

  /** The reader interrupted the automatic attempts and wants to choose. */
  void showNetworkList(uint32_t nowMs);

  /** The reader asked for a fresh scan. */
  void rescan(uint32_t nowMs);

  /** The reader asked to drop the stored password for the network at `index`. */
  void forgetNetwork(size_t index, uint32_t nowMs);

  /** The reader acknowledged a failed join. */
  void dismissFailure(uint32_t nowMs);

  /** The reader left the screen. */
  void cancel(uint32_t nowMs);

  /**
   * Pops the next thing for the activity to do, or returns false when there is
   * nothing to do at `nowMs`. Call it in a loop until it returns false.
   */
  bool nextAction(uint32_t nowMs, Action& action);

  State state() const { return state_; }
  /**
   * True while the screen should ask whether to remember the password. Only a
   * password the reader typed is worth offering; a stored one is already saved.
   */
  bool offersToSaveCredential() const { return offersToSave_; }
  /** The network being joined, or the one just joined. */
  const std::string& activeSsid() const { return activeSsid_; }
  const std::vector<Network>& networks() const { return networks_; }

 private:
  void checkTimeouts(uint32_t nowMs);
  bool isSaved(const char* ssid) const;
  bool alreadyTriedAutomatically(const char* ssid) const;
  void queue(ActionKind kind, const std::string& ssid = {});
  void queueJoin(const std::string& ssid, bool useSavedPassword);
  void startScan(uint32_t nowMs);
  /** Joins `ssid` outright when we hold its password, and asks for one otherwise. */
  void joinOrAskForPassword(const std::string& ssid, bool hasSavedPassword, bool isEncrypted);
  /** Joins the strongest scanned network with a credential we have not tried yet. */
  bool tryNextSavedNetworkFromScan();

  Startup startup_;
  std::vector<Network> networks_;
  std::vector<Action> pending_;
  std::vector<std::string> autoAttemptedSsids_;
  std::string joiningSsid_;
  std::string activeSsid_;
  bool typedPasswordPending_ = false;
  bool offersToSave_ = false;
  State state_ = State::SCANNING;
  uint32_t clockMs_ = 0;
  uint32_t scanStartedMs_ = 0;
  uint32_t joinStartedMs_ = 0;
};

}  // namespace wifi_session
