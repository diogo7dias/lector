#pragma once

// Decision rules for unattended KOReader sync: when the firmware may spend
// radio time on its own, and which side of a progress comparison wins.
//
// Manual "Sync Progress" stays what it always was — the user asks for it, picks
// a network if none is saved, and may be shown an Apply/Upload choice. Auto sync
// has no user in front of it, so every path here is deliberately narrow: it runs
// only from the reader, only on a deliberate action, and only when the device
// can join a network without asking anything.
//
// The pending-pull struct lives in RTC memory across the silent reboot that
// separates the network stage from the reading stage (see KOReaderAutoSync.h).
// RTC_NOINIT memory is not zeroed on a software reset, so a magic stamp plus a
// bounds check on the stored length is what distinguishes a real handoff from
// whatever bytes happened to be there.
//
// Host-testable: no SD, no display, no ESP headers.

#include <cmath>
#include <cstdint>

namespace ko_auto_sync {

// Everything auto sync needs to know about the device's configuration.
struct Gate {
  bool autoSyncEnabled = false;     // The in-book menu toggle.
  bool hasSyncCredentials = false;  // KOReader username and password are set.
  bool hasWifiCredentials = false;  // At least one saved network to join unattended.
};

enum class SleepCause : uint8_t {
  PowerButton,        // The user held the power button: a deliberate "I am done for now".
  InactivityTimeout,  // The device put itself to sleep; nobody is watching.
};

inline bool gateIsOpen(const Gate& gate) {
  return gate.autoSyncEnabled && gate.hasSyncCredentials && gate.hasWifiCredentials;
}

inline bool shouldPushOnSleep(const Gate& gate, const SleepCause cause, const bool inReader) {
  if (!gateIsOpen(gate)) return false;
  if (!inReader) return false;
  // An unattended timeout would pay ~4 seconds of radio for a page the user may
  // not even have read, every time the device is left open. Only a deliberate
  // lock is worth the battery.
  return cause == SleepCause::PowerButton;
}

inline bool shouldPushOnLeavingBook(const Gate& gate) { return gateIsOpen(gate); }

inline bool shouldPullOnBookOpen(const Gate& gate) { return gateIsOpen(gate); }

// Progress within this much of each other counts as the same place. Matches the
// epsilon the manual smart sync uses (KOReaderSyncActivity::performSync), so the
// two paths cannot disagree about whether two devices are already in step.
inline constexpr float kSameProgressEpsilon = 0.001f;  // 0.1 percentage points

inline bool remoteIsFurther(const float localPercentage, const float remotePercentage) {
  const float delta = remotePercentage - localPercentage;
  if (std::fabs(delta) <= kSameProgressEpsilon) return false;
  return delta > 0.0f;
}

// Longest XPath the sync server stores (see KOReaderRichPosition::xpath).
inline constexpr uint16_t kMaxXPathLength = 120;

// Stamp identifying a deliberately written handoff in uninitialised RTC memory.
inline constexpr uint32_t kPendingPullMagic = 0x4B4F5041;  // "KOPA"

// Remote position carried across the silent reboot between the network stage
// and the reading stage. Kept flat and trivially copyable: it is written into
// RTC_NOINIT memory, which cannot hold anything that owns heap.
struct PendingPull {
  uint32_t magic = 0;
  uint16_t xpathLength = 0;
  char xpath[kMaxXPathLength + 1] = {};
  float percentage = 0.0f;
  uint16_t spineIndex = 0;
  uint16_t paragraphIndex = 0;
  bool hasParagraphIndex = false;
};

inline bool isPendingPullValid(const PendingPull& pending) {
  if (pending.magic != kPendingPullMagic) return false;
  if (pending.xpathLength == 0) return false;
  return pending.xpathLength <= kMaxXPathLength;
}

}  // namespace ko_auto_sync
