#pragma once

#include <cstdint>
#include <optional>

// Session-only deliberate-jump origins. Ordinary page turns never call recordJump().
// Return walks back through them; a Return that lands keeps the page it left on a
// forward list, so Forward redoes it (and a landed Forward puts its page back on the
// Return list). A new deliberate jump starts a fresh branch and drops the forward list.
class ReaderReturnHistory {
 public:
  struct Position {
    int32_t spineIndex;
    uint32_t contentOffset;
  };
  static constexpr uint8_t CAPACITY = 8;
  static_assert(sizeof(Position) * CAPACITY == 64);

  bool empty() const { return back.count == 0; }
  bool forwardEmpty() const { return forward.count == 0; }

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
