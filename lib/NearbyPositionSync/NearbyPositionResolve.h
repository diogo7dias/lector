#pragma once

namespace nearby_position {
enum class Resolution { Same, KeepLocal, TakePeer };

// Comparisons stay in SyncSession; this policy is independent of radio and UI.
constexpr Resolution resolvePosition(bool positionsMatch, bool peerIsFurtherAlong) {
  return positionsMatch ? Resolution::Same : peerIsFurtherAlong ? Resolution::TakePeer : Resolution::KeepLocal;
}
}  // namespace nearby_position
