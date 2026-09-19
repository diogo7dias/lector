#pragma once

#include <cstdint>
#include <optional>

// Session-only deliberate-jump origins. Ordinary page turns never call recordJump().
class ReaderReturnHistory {
 public:
  struct Position {
    int32_t spineIndex;
    uint32_t contentOffset;
  };
  static constexpr uint8_t CAPACITY = 8;
  static_assert(sizeof(Position) * CAPACITY == 64);

  bool empty() const { return count == 0; }

  void recordJump(Position origin, bool accepted = true) {
    if (!accepted) return;
    returning = false;
    entries[next] = origin;
    next = (next + 1) % CAPACITY;
    if (count < CAPACITY) ++count;
  }

  std::optional<Position> beginReturn() {
    if (empty()) return std::nullopt;
    returning = true;
    return entries[(next + CAPACITY - 1) % CAPACITY];
  }

  // A failed destination (or navigation superseding Return) keeps the entry retryable.
  void finishReturn(bool loaded) {
    if (returning && loaded) {
      next = (next + CAPACITY - 1) % CAPACITY;
      --count;
    }
    returning = false;
  }

 private:
  Position entries[CAPACITY]{};
  uint8_t next = 0;
  uint8_t count = 0;
  bool returning = false;
};
