#include "WifiSession.h"

#include <DiagLog.h>

#include <algorithm>
#include <cstring>

namespace wifi_session {

const char* WifiSession::stateName(const State state) {
  switch (state) {
    case State::AUTO_CONNECTING:
      return "AUTO_CONNECTING";
    case State::SCANNING:
      return "SCANNING";
    case State::NETWORK_LIST:
      return "NETWORK_LIST";
    case State::CONNECTING:
      return "CONNECTING";
    case State::CONNECTED:
      return "CONNECTED";
    case State::FAILED:
      return "FAILED";
  }
  return "unknown";
}

void WifiSession::setState(const State state) {
  if (state == state_) return;
  diaglog::note("  t=%u state %s -> %s previous_ms=%u", clockMs_, stateName(state_), stateName(state),
                clockMs_ - stateSinceMs_);
  state_ = state;
  stateSinceMs_ = clockMs_;
  ++transitions_;
}

void WifiSession::noteDiagnostics(const uint32_t nowMs) const {
  if (!begun_) {
    diaglog::note("  t=%u session=not_started state=%s", nowMs, stateName(state_));
    return;
  }
  diaglog::note("  t=%u state=%s since=%u transitions=%u joins=%u scans=%u fired=%u", nowMs, stateName(state_),
                stateSinceMs_, transitions_, joins_, scans_, timeouts_);
  diaglog::note("  nextAction/checkTimeouts=%u last=%u age_ms=%u max_gap_ms=%u queued=%u auto_tried=%u", drains_,
                lastDrainMs_, nowMs - lastDrainMs_, maxDrainGapMs_, static_cast<unsigned>(pending_.size()),
                static_cast<unsigned>(autoAttemptedSsids_.size()));
  if (joins_ != 0) {
    diaglog::note("  last_join auto=%d saved=%d at=%u target_index=%d strongest_rssi=%d scan_count=%d target_seen=%d",
                  lastJoinAutomatic_, lastJoinSaved_, joinStartedMs_, targetIndex_, targetRssi_, scanCount_,
                  scannedTarget_);
  } else {
    diaglog::note("  last_join=none");
  }
  const bool joining = !joiningSsid_.empty() && (state_ == State::CONNECTING || state_ == State::AUTO_CONNECTING);
  const bool scanning = state_ == State::SCANNING || (state_ == State::AUTO_CONNECTING && joiningSsid_.empty());
  const uint32_t budget =
      joining ? (state_ == State::AUTO_CONNECTING ? AUTO_JOIN_TIMEOUT_MS : JOIN_TIMEOUT_MS) : SCAN_TIMEOUT_MS;
  const uint32_t started = joining ? joinStartedMs_ : scanStartedMs_;
  diaglog::note("  timer=%s budget=%u armed=%u expires=%u elapsed=%u due=%d",
                joining    ? "join"
                : scanning ? "scan"
                           : "off",
                budget, started, started + budget, nowMs - started, (joining || scanning) && nowMs - started >= budget);
}

void WifiSession::queue(const ActionKind kind, const std::string& ssid) {
  Action action;
  action.kind = kind;
  action.ssid = ssid;
  pending_.push_back(action);
  if (kind == ActionKind::START_SCAN) {
    ++scans_;
    diaglog::note("  t=%u scan arm budget=%u expires=%u scan=%u", clockMs_, SCAN_TIMEOUT_MS, clockMs_ + SCAN_TIMEOUT_MS,
                  scans_);
  }
}

void WifiSession::checkTimeouts(const uint32_t nowMs) {
  const bool scanning = state_ == State::SCANNING || (state_ == State::AUTO_CONNECTING && joiningSsid_.empty());
  if (scanning && nowMs - scanStartedMs_ >= SCAN_TIMEOUT_MS) {
    // A scan that answers neither way leaves the reader looking at an empty list
    // rather than at a spinner that never stops.
    ++timeouts_;
    diaglog::note("  t=%u timeout fired scan elapsed=%u", nowMs, nowMs - scanStartedMs_);
    networks_.clear();
    setState(State::NETWORK_LIST);
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

  ++timeouts_;
  diaglog::note("  t=%u timeout fired %s budget=%u elapsed=%u", nowMs, automatic ? "auto" : "manual", budget,
                nowMs - joinStartedMs_);
  joiningSsid_.clear();
  if (automatic) {
    queue(ActionKind::DISCONNECT);
    queue(ActionKind::START_SCAN);
    scanStartedMs_ = nowMs;
    return;
  }
  queue(ActionKind::DISCONNECT);
  setState(State::FAILED);
}

bool WifiSession::nextAction(const uint32_t nowMs, Action& action) {
  clockMs_ = nowMs;
  if (drains_ != 0) maxDrainGapMs_ = std::max(maxDrainGapMs_, nowMs - lastDrainMs_);
  lastDrainMs_ = nowMs;
  ++drains_;
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
  ++joins_;
  const uint32_t budget = state_ == State::AUTO_CONNECTING ? AUTO_JOIN_TIMEOUT_MS : JOIN_TIMEOUT_MS;
  diaglog::note("  t=%u join=%u mode=%s saved=%d budget=%u expires=%u", clockMs_, joins_,
                state_ == State::AUTO_CONNECTING ? "auto" : "manual", useSavedPassword, budget, clockMs_ + budget);
  lastJoinAutomatic_ = state_ == State::AUTO_CONNECTING;
  lastJoinSaved_ = useSavedPassword;
  targetIndex_ = -1;
  targetRssi_ = 0;
  for (size_t i = 0; i < networks_.size(); ++i) {
    if (ssid == networks_[i].ssid) {
      targetIndex_ = static_cast<int>(i);
      targetRssi_ = networks_[i].rssi;
      break;
    }
  }
  scannedTarget_ = targetIndex_ >= 0 ? 1 : scanCount_ < 0 ? -1 : 0;
  queue(ActionKind::JOIN, ssid);
  pending_.back().useSavedPassword = useSavedPassword;
}

void WifiSession::begin(const Startup& startup, const uint32_t nowMs) {
  begun_ = true;
  stateSinceMs_ = nowMs;
  scanCount_ = scannedTarget_ = targetIndex_ = -1;
  targetRssi_ = 0;
  lastJoinAutomatic_ = lastJoinSaved_ = false;
  transitions_ = drains_ = maxDrainGapMs_ = joins_ = scans_ = timeouts_ = 0;
  lastDrainMs_ = nowMs;
  diaglog::note("  t=%u session begin state=%s auto_allowed=%d saved_count=%u", nowMs, stateName(state_),
                startup.allowAutoConnect, static_cast<unsigned>(startup.savedSsids.size()));
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
    setState(State::AUTO_CONNECTING);
    autoAttemptedSsids_.push_back(startup_.lastConnectedSsid);
    queueJoin(startup_.lastConnectedSsid, true);
    return;
  }

  setState(State::SCANNING);
  if (canAutoConnect) {
    setState(State::AUTO_CONNECTING);
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
  diaglog::note("  t=%u join failed (radio)", nowMs);
  joiningSsid_.clear();
  // A typed password that did not join has nothing worth remembering, and left set
  // it would offer to save itself over whichever network joins next.
  typedPasswordPending_ = false;
  if (state_ != State::AUTO_CONNECTING) {
    setState(State::FAILED);
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
  setState(State::CONNECTING);
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
  setState(State::SCANNING);
  scanStartedMs_ = nowMs;
  queue(ActionKind::START_SCAN);
}

void WifiSession::onScanFailed(const uint32_t nowMs) {
  clockMs_ = nowMs;
  diaglog::note("  t=%u scan failed (radio)", nowMs);
  networks_.clear();
  setState(State::NETWORK_LIST);
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
  setState(State::NETWORK_LIST);
}

void WifiSession::cancel(const uint32_t nowMs) {
  clockMs_ = nowMs;
  queue(ActionKind::FINISH);
}

void WifiSession::abandonPasswordEntry(const uint32_t nowMs) {
  clockMs_ = nowMs;
  typedPasswordPending_ = false;
  setState(State::NETWORK_LIST);
}

void WifiSession::onPasswordEntered(const uint32_t nowMs) {
  clockMs_ = nowMs;
  setState(State::CONNECTING);
  queueJoin(activeSsid_, false);
}

void WifiSession::onJoinSucceeded(const uint32_t nowMs) {
  clockMs_ = nowMs;
  joiningSsid_.clear();
  setState(State::CONNECTED);
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
  bool targetSeen = false;
  for (size_t i = 0; i < count; ++i)
    if (activeSsid_ == found[i].ssid) targetSeen = true;
  scanCount_ = static_cast<int>(count);
  scannedTarget_ = activeSsid_.empty() ? -1 : targetSeen ? 1 : 0;
  diaglog::note("  t=%u scan results=%u previous_target=%s", nowMs, static_cast<unsigned>(count),
                activeSsid_.empty() ? "none"
                : targetSeen        ? "present"
                                    : "absent");
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
    setState(State::NETWORK_LIST);
  }
}

}  // namespace wifi_session
