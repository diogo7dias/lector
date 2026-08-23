#include "WifiSession.h"

#include <algorithm>
#include <cstring>

namespace wifi_session {

void WifiSession::queue(const ActionKind kind, const std::string& ssid) {
  Action action;
  action.kind = kind;
  action.ssid = ssid;
  pending_.push_back(action);
}

void WifiSession::checkTimeouts(const uint32_t nowMs) {
  const bool scanning = state_ == State::SCANNING || (state_ == State::AUTO_CONNECTING && joiningSsid_.empty());
  if (scanning && nowMs - scanStartedMs_ >= SCAN_TIMEOUT_MS) {
    // A scan that answers neither way leaves the reader looking at an empty list
    // rather than at a spinner that never stops.
    networks_.clear();
    state_ = State::NETWORK_LIST;
    return;
  }

  const bool automatic = state_ == State::AUTO_CONNECTING;
  const bool joining = !joiningSsid_.empty() && (automatic || state_ == State::CONNECTING);
  if (!joining) {
    return;
  }

  const uint32_t budget = automatic ? AUTO_JOIN_TIMEOUT_MS : JOIN_TIMEOUT_MS;
  if (nowMs - joinStartedMs_ < budget) {
    return;
  }

  joiningSsid_.clear();
  if (automatic) {
    queue(ActionKind::DISCONNECT);
    queue(ActionKind::START_SCAN);
    scanStartedMs_ = nowMs;
    return;
  }
  queue(ActionKind::DISCONNECT);
  state_ = State::FAILED;
}

bool WifiSession::nextAction(const uint32_t nowMs, Action& action) {
  clockMs_ = nowMs;
  checkTimeouts(nowMs);
  if (pending_.empty()) {
    return false;
  }
  action = pending_.front();
  pending_.erase(pending_.begin());
  return true;
}

void WifiSession::queueJoin(const std::string& ssid, const bool useSavedPassword) {
  joiningSsid_ = ssid;
  activeSsid_ = ssid;
  joinStartedMs_ = clockMs_;
  queue(ActionKind::JOIN, ssid);
  pending_.back().useSavedPassword = useSavedPassword;
}

void WifiSession::begin(const Startup& startup, const uint32_t nowMs) {
  startup_ = startup;
  clockMs_ = nowMs;
  scanStartedMs_ = nowMs;
  joinStartedMs_ = nowMs;
  networks_.clear();
  pending_.clear();
  autoAttemptedSsids_.clear();
  joiningSsid_.clear();

  const bool canAutoConnect = startup_.allowAutoConnect && !startup_.savedSsids.empty();
  if (canAutoConnect && isSaved(startup_.lastConnectedSsid.c_str())) {
    state_ = State::AUTO_CONNECTING;
    autoAttemptedSsids_.push_back(startup_.lastConnectedSsid);
    queueJoin(startup_.lastConnectedSsid, true);
    return;
  }

  state_ = State::SCANNING;
  if (canAutoConnect) {
    state_ = State::AUTO_CONNECTING;
  }
  queue(ActionKind::START_SCAN);
}

bool WifiSession::alreadyTriedAutomatically(const char* ssid) const {
  for (const std::string& tried : autoAttemptedSsids_) {
    if (tried == ssid) {
      return true;
    }
  }
  return false;
}

bool WifiSession::tryNextSavedNetworkFromScan() {
  for (const Network& network : networks_) {
    if (!network.hasSavedPassword || alreadyTriedAutomatically(network.ssid)) {
      continue;
    }
    autoAttemptedSsids_.push_back(network.ssid);
    queueJoin(network.ssid, true);
    return true;
  }
  return false;
}

void WifiSession::onJoinFailed(const uint32_t nowMs) {
  clockMs_ = nowMs;
  joiningSsid_.clear();
  if (state_ != State::AUTO_CONNECTING) {
    state_ = State::FAILED;
    return;
  }

  // Every saved network gets one attempt per session; a fresh scan may show one
  // that was out of range when the screen opened.
  queue(ActionKind::DISCONNECT);
  queue(ActionKind::START_SCAN);
  scanStartedMs_ = nowMs;
}

void WifiSession::joinOrAskForPassword(const std::string& ssid, const bool hasSavedPassword, const bool isEncrypted) {
  if (isEncrypted && !hasSavedPassword) {
    activeSsid_ = ssid;
    typedPasswordPending_ = true;
    queue(ActionKind::ASK_FOR_PASSWORD, activeSsid_);
    return;
  }
  state_ = State::CONNECTING;
  queueJoin(ssid, hasSavedPassword);
}

void WifiSession::selectNetwork(const size_t index, const uint32_t nowMs) {
  if (index >= networks_.size()) {
    return;
  }
  clockMs_ = nowMs;
  const Network& picked = networks_[index];
  joinOrAskForPassword(picked.ssid, picked.hasSavedPassword, picked.isEncrypted);
}

void WifiSession::selectHiddenNetwork(const std::string& ssid, const uint32_t nowMs) {
  if (ssid.empty()) {
    return;
  }
  clockMs_ = nowMs;
  // A hidden network never appears in a scan, so its encryption is unknown; ask
  // for a password unless one is already stored.
  joinOrAskForPassword(ssid, isSaved(ssid.c_str()), true);
}

void WifiSession::startScan(const uint32_t nowMs) {
  state_ = State::SCANNING;
  scanStartedMs_ = nowMs;
  queue(ActionKind::START_SCAN);
}

void WifiSession::onScanFailed(const uint32_t nowMs) {
  clockMs_ = nowMs;
  networks_.clear();
  state_ = State::NETWORK_LIST;
}

void WifiSession::showNetworkList(const uint32_t nowMs) {
  clockMs_ = nowMs;
  joiningSsid_.clear();
  // Once the reader takes over, no further network is tried behind their back.
  startup_.allowAutoConnect = false;
  queue(ActionKind::DISCONNECT);
  startScan(nowMs);
}

void WifiSession::rescan(const uint32_t nowMs) {
  clockMs_ = nowMs;
  startScan(nowMs);
}

void WifiSession::forgetNetwork(const size_t index, const uint32_t nowMs) {
  if (index >= networks_.size() || !networks_[index].hasSavedPassword) {
    return;
  }
  clockMs_ = nowMs;
  queue(ActionKind::FORGET_CREDENTIAL, networks_[index].ssid);
  for (auto it = startup_.savedSsids.begin(); it != startup_.savedSsids.end(); ++it) {
    if (*it == networks_[index].ssid) {
      startup_.savedSsids.erase(it);
      break;
    }
  }
  startScan(nowMs);
}

void WifiSession::dismissFailure(const uint32_t nowMs) {
  clockMs_ = nowMs;
  // Back to the list, whatever failed. A failure is usually the router, not the
  // password, and offering to delete the credential every time trains the reader
  // to throw away a working password over a passing failure.
  state_ = State::NETWORK_LIST;
}

void WifiSession::cancel(const uint32_t nowMs) {
  clockMs_ = nowMs;
  queue(ActionKind::FINISH);
}

void WifiSession::abandonPasswordEntry(const uint32_t nowMs) {
  clockMs_ = nowMs;
  typedPasswordPending_ = false;
  state_ = State::NETWORK_LIST;
}

void WifiSession::onPasswordEntered(const uint32_t nowMs) {
  clockMs_ = nowMs;
  state_ = State::CONNECTING;
  queueJoin(activeSsid_, false);
}

void WifiSession::onJoinSucceeded(const uint32_t nowMs) {
  clockMs_ = nowMs;
  joiningSsid_.clear();
  state_ = State::CONNECTED;
  if (typedPasswordPending_) {
    offersToSave_ = true;
    return;
  }
  queue(ActionKind::FINISH);
  pending_.back().connected = true;
}

void WifiSession::answerSavePrompt(const bool remember, const uint32_t nowMs) {
  clockMs_ = nowMs;
  offersToSave_ = false;
  typedPasswordPending_ = false;
  if (remember) {
    queue(ActionKind::SAVE_CREDENTIAL, activeSsid_);
  }
  queue(ActionKind::FINISH);
  pending_.back().connected = true;
}

bool WifiSession::isSaved(const char* ssid) const {
  for (const std::string& saved : startup_.savedSsids) {
    if (saved == ssid) {
      return true;
    }
  }
  return false;
}

void WifiSession::onScanResults(const Network* found, const size_t count, const uint32_t nowMs) {
  clockMs_ = nowMs;
  networks_.clear();
  for (size_t i = 0; i < count; ++i) {
    const Network& sighting = found[i];
    Network* existing = nullptr;
    for (Network& kept : networks_) {
      if (strcmp(kept.ssid, sighting.ssid) == 0) {
        existing = &kept;
        break;
      }
    }
    if (existing == nullptr) {
      networks_.push_back(sighting);
      networks_.back().hasSavedPassword = isSaved(sighting.ssid);
    } else if (sighting.rssi > existing->rssi) {
      const bool saved = existing->hasSavedPassword;
      *existing = sighting;
      existing->hasSavedPassword = saved;
    }
  }

  // A network we can join without asking is worth more than a strong stranger.
  std::stable_sort(networks_.begin(), networks_.end(), [](const Network& a, const Network& b) {
    if (a.hasSavedPassword != b.hasSavedPassword) {
      return a.hasSavedPassword;
    }
    return a.rssi > b.rssi;
  });

  if (state_ == State::AUTO_CONNECTING && tryNextSavedNetworkFromScan()) {
    return;
  }
  if (state_ == State::SCANNING || state_ == State::AUTO_CONNECTING) {
    state_ = State::NETWORK_LIST;
  }
}

}  // namespace wifi_session
