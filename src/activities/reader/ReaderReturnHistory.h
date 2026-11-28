#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

// Deliberate-jump origins, persisted next to links.bin so Return survives sleep.
// Ordinary page turns never call recordJump(). Return walks back through them; a Return
// that lands keeps the page it left on a forward list, so Forward redoes it (and a landed
// Forward puts its page back on the Return list). A new deliberate jump starts a fresh
// branch and drops the forward list.
class ReaderReturnHistory {
 public:
  struct Position {
    int32_t spineIndex;
    uint32_t contentOffset;
  };
  static constexpr uint8_t CAPACITY = 8;
  static_assert(sizeof(Position) * CAPACITY == 64);
  // Serialized layout: [uint8_t count] followed by `count` entries oldest-to-newest,
  // each encoded as little-endian (int32_t spineIndex, uint32_t contentOffset).
  static constexpr size_t MAX_SERIALIZED_BYTES = 1 + sizeof(Position) * CAPACITY;

  bool empty() const { return back.count == 0; }
  bool forwardEmpty() const { return forward.count == 0; }
  uint8_t count() const { return back.count; }

  // Packs the Return ring (oldest to newest) into `out`. Returns bytes written, or 0
  // when empty or `outSize` is too small. Uses byte writes for RISC-V alignment safety.
  size_t serialize(uint8_t* out, const size_t outSize) const {
    if (!out || back.count == 0) return 0;
    const size_t needed = 1 + static_cast<size_t>(back.count) * 8;
    if (outSize < needed) return 0;
    out[0] = back.count;
    const uint8_t oldest = static_cast<uint8_t>((back.next + CAPACITY - back.count) % CAPACITY);
    size_t pos = 1;
    for (uint8_t i = 0; i < back.count; ++i) {
      const Position& entry = back.entries[(oldest + i) % CAPACITY];
      const uint32_t spine = static_cast<uint32_t>(entry.spineIndex);
      const uint32_t offset = entry.contentOffset;
      out[pos++] = static_cast<uint8_t>(spine & 0xFF);
      out[pos++] = static_cast<uint8_t>((spine >> 8) & 0xFF);
      out[pos++] = static_cast<uint8_t>((spine >> 16) & 0xFF);
      out[pos++] = static_cast<uint8_t>((spine >> 24) & 0xFF);
      out[pos++] = static_cast<uint8_t>(offset & 0xFF);
      out[pos++] = static_cast<uint8_t>((offset >> 8) & 0xFF);
      out[pos++] = static_cast<uint8_t>((offset >> 16) & 0xFF);
      out[pos++] = static_cast<uint8_t>((offset >> 24) & 0xFF);
    }
    return pos;
  }

  // Restores the Return ring from bytes written by serialize(). Rejects malformed sizes
  // or entries whose spineIndex is outside [0, spineCount).
  bool deserialize(const uint8_t* data, const size_t size, const int spineCount) {
    if (!data || size < 1 || spineCount <= 0) return false;
    const uint8_t n = data[0];
    if (n < 1 || n > CAPACITY || size != 1 + static_cast<size_t>(n) * 8) return false;
    Position parsed[CAPACITY]{};
    for (uint8_t i = 0; i < n; ++i) {
      const uint8_t* p = data + 1 + static_cast<size_t>(i) * 8;
      const uint32_t rawSpine = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
      const int32_t spine = static_cast<int32_t>(rawSpine);
      const uint32_t offset = static_cast<uint32_t>(p[4]) | (static_cast<uint32_t>(p[5]) << 8) |
                              (static_cast<uint32_t>(p[6]) << 16) | (static_cast<uint32_t>(p[7]) << 24);
      if (spine < 0 || spine >= spineCount) return false;
      parsed[i] = {spine, offset};
    }
    back = Ring{};
    forward = Ring{};
    leaving.reset();
    stepping = Step::None;
    for (uint8_t i = 0; i < n; ++i) back.push(parsed[i]);
    return true;
  }

  void recordJump(Position origin, bool accepted = true) {
    if (!accepted) return;
    stepping = Step::None;
    back.push(origin);
    forward.count = 0;
  }

  // `from` is the page being left; without one the step still lands, it just cannot be
  // stepped back to.
  std::optional<Position> beginReturn(std::optional<Position> from = std::nullopt) {
    return begin(back, Step::Back, from);
  }
  std::optional<Position> beginForward(std::optional<Position> from = std::nullopt) {
    return begin(forward, Step::Forward, from);
  }

  // Every landing path calls this. A failed destination (or navigation superseding the
  // step) keeps the entry retryable.
  void finishReturn(bool loaded) {
    if (loaded && stepping != Step::None) {
      Ring& source = stepping == Step::Back ? back : forward;
      Ring& other = stepping == Step::Back ? forward : back;
      source.pop();
      if (leaving) other.push(*leaving);
    }
    stepping = Step::None;
  }

 private:
  enum class Step : uint8_t { None, Back, Forward };

  // Newest-last ring; a push past CAPACITY drops the oldest.
  struct Ring {
    Position entries[CAPACITY]{};
    uint8_t next = 0;
    uint8_t count = 0;
    void push(Position p) {
      entries[next] = p;
      next = (next + 1) % CAPACITY;
      if (count < CAPACITY) ++count;
    }
    Position top() const { return entries[(next + CAPACITY - 1) % CAPACITY]; }
    void pop() {
      next = (next + CAPACITY - 1) % CAPACITY;
      --count;
    }
  };

  std::optional<Position> begin(const Ring& ring, Step step, std::optional<Position> from) {
    if (ring.count == 0) return std::nullopt;
    stepping = step;
    leaving = from;
    return ring.top();
  }

  Ring back;
  Ring forward;
  std::optional<Position> leaving;
  Step stepping = Step::None;
};
