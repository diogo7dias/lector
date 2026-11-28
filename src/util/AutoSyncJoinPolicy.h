#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Pure decision logic for lock-screen Auto Sync Wi-Fi joining and failure backoff.
// Kept hardware-free so host tests cover scan filtering, hidden SSIDs, and cooldowns.
namespace auto_sync_join {

constexpr size_t MAX_JOINS = 3;
constexpr int64_t BASE_COOLDOWN_S = 5 * 60;  // 5 min after first failure
constexpr int64_t MAX_COOLDOWN_S = 60 * 60;  // 60 min ceiling
constexpr uint8_t MAX_FAILURE_STREAK = 8;
constexpr uint8_t MAX_UNSET_CLOCK_SKIPS = 8;
constexpr uint32_t BACKOFF_MAGIC = 0x4B4F424Fu;  // "KOBO"

struct SavedNetwork {
  std::string ssid;
  bool hidden = false;
};

struct ScannedNetwork {
  std::string ssid;
  int32_t rssi = -100;
};

// RTC-friendly POD layout (no constructors/destructors so it can live in RTC_NOINIT_ATTR).
struct BackoffState {
  uint32_t magic;
  uint8_t consecutiveFailures;
  uint8_t skipsRemaining;
  int64_t failedAtEpoch;
  int64_t nextRetryEpoch;
};

inline void ensureInitialized(BackoffState& state) {
  if (state.magic != BACKOFF_MAGIC) {
    state.magic = BACKOFF_MAGIC;
    state.consecutiveFailures = 0;
    state.skipsRemaining = 0;
    state.failedAtEpoch = 0;
    state.nextRetryEpoch = 0;
  }
}

inline int64_t cooldownSecondsForFailures(const uint8_t failures) {
  if (failures == 0) return 0;
  const uint8_t shift = failures > 5 ? 4 : static_cast<uint8_t>(failures - 1);
  const int64_t seconds = BASE_COOLDOWN_S << shift;
  return seconds > MAX_COOLDOWN_S ? MAX_COOLDOWN_S : seconds;
}

// True when lock-screen Auto Sync should skip turning Wi-Fi on due to prior failures.
// When `nowEpoch > 0` (RTC/NTP valid), enforces wall-clock cooldown; a clock that stepped
// backward before `failedAtEpoch` does not trap sync in cooldown. When `nowEpoch == 0`,
// falls back to skipping a bounded number of locks.
inline bool shouldSkip(BackoffState& state, const int64_t nowEpoch) {
  ensureInitialized(state);
  if (state.consecutiveFailures == 0) return false;
  if (nowEpoch > 0 && state.nextRetryEpoch > 0) {
    if (nowEpoch < state.failedAtEpoch) return false;
    return nowEpoch < state.nextRetryEpoch;
  }
  return state.skipsRemaining > 0;
}

inline void consumeSkippedLock(BackoffState& state, const int64_t nowEpoch) {
  ensureInitialized(state);
  if ((nowEpoch <= 0 || state.nextRetryEpoch <= 0) && state.skipsRemaining > 0) {
    --state.skipsRemaining;
  }
}

inline void recordFailure(BackoffState& state, const int64_t nowEpoch) {
  ensureInitialized(state);
  if (state.consecutiveFailures < MAX_FAILURE_STREAK) {
    ++state.consecutiveFailures;
  }
  const int64_t cooldownS = cooldownSecondsForFailures(state.consecutiveFailures);
  state.failedAtEpoch = nowEpoch;
  state.nextRetryEpoch = nowEpoch > 0 ? nowEpoch + cooldownS : 0;
  const uint8_t shift = state.consecutiveFailures > 4 ? 3 : static_cast<uint8_t>(state.consecutiveFailures - 1);
  const uint8_t skips = static_cast<uint8_t>(1u << shift);
  state.skipsRemaining = skips > MAX_UNSET_CLOCK_SKIPS ? MAX_UNSET_CLOCK_SKIPS : skips;
}

inline void recordSuccess(BackoffState& state) {
  state.magic = BACKOFF_MAGIC;
  state.consecutiveFailures = 0;
  state.skipsRemaining = 0;
  state.failedAtEpoch = 0;
  state.nextRetryEpoch = 0;
}

// Chooses up to `maxJoins` saved SSIDs to attempt after one fast scan:
//   1. Saved SSIDs present in `scanned`: `lastConnectedSsid` first, then strongest RSSI.
//   2. Configured hidden saved SSIDs not already listed (`lastConnectedSsid` first if hidden).
// Returns empty when no saved SSID was seen and none is configured as hidden, avoiding
// blind 7 s join timeouts away from home.
inline std::vector<std::string> selectCandidates(const std::vector<SavedNetwork>& saved,
                                                 const std::vector<ScannedNetwork>& scanned,
                                                 const std::string& lastConnectedSsid,
                                                 const size_t maxJoins = MAX_JOINS) {
  std::vector<std::string> out;
  if (saved.empty() || maxJoins == 0) return out;
  out.reserve(std::min(saved.size(), maxJoins));

  struct Match {
    std::string ssid;
    int32_t rssi;
    bool isLast;
  };
  std::vector<Match> seenMatches;
  seenMatches.reserve(saved.size());

  for (const auto& cred : saved) {
    if (cred.ssid.empty()) continue;
    bool found = false;
    int32_t bestRssi = INT32_MIN;
    for (const auto& ap : scanned) {
      if (ap.ssid == cred.ssid) {
        if (!found || ap.rssi > bestRssi) bestRssi = ap.rssi;
        found = true;
      }
    }
    if (found) {
      seenMatches.push_back({cred.ssid, bestRssi, cred.ssid == lastConnectedSsid});
    }
  }

  std::stable_sort(seenMatches.begin(), seenMatches.end(), [](const Match& a, const Match& b) {
    if (a.isLast != b.isLast) return a.isLast;
    return a.rssi > b.rssi;
  });

  const auto alreadyListed = [&](const std::string& ssid) {
    return std::find(out.begin(), out.end(), ssid) != out.end();
  };

  for (const auto& m : seenMatches) {
    if (out.size() >= maxJoins) return out;
    if (!alreadyListed(m.ssid)) out.push_back(m.ssid);
  }

  // Direct-join only for credentials explicitly configured as hidden networks.
  for (const bool wantLast : {true, false}) {
    for (const auto& cred : saved) {
      if (out.size() >= maxJoins) return out;
      if (!cred.hidden || cred.ssid.empty()) continue;
      if ((cred.ssid == lastConnectedSsid) != wantLast) continue;
      if (!alreadyListed(cred.ssid)) out.push_back(cred.ssid);
    }
  }

  return out;
}

}  // namespace auto_sync_join
