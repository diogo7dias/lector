#pragma once

// Replaying a side key's held-back edges.
//
// While the bindings router decides which gesture a side key was, that key's raw
// edges are hidden from the rest of the firmware — nothing can tell a single
// click from the first half of a double until the window closes. When the router
// rules the gesture to be the paging the key already does, the edges it held back
// have to be handed over after the fact.
//
// That replay is two passes long, and both halves are load-bearing:
//
//   - the PRESS on the pass the router rules the gesture. Every side-key consumer
//     in the firmware steps on the press (ReaderUtils::detectPageTurn,
//     ButtonNavigator::onStep), so dropping it drops the page turn;
//   - the RELEASE on the pass after. Release-stepping lists take it, and repeat
//     runs end on it.
//
// They cannot share a pass: no physical key ever delivers a press and a release
// in one frame, and a consumer that saw both would move twice.
//
// Pure state, no Arduino: this is the rule, not the plumbing. main.cpp owns the
// GPIO poll and the override call; this owns when a release is owed.
namespace button_replay {

// What to hand to MappedInputManager::setSideKeyOverride this pass.
struct Injection {
  bool press = false;
  bool release = false;
};

// One side key's replay state. Two of these live across loop passes.
class SideKey {
 public:
  // `intercepted` is Router::intercepts(key): false means this key is left alone
  // entirely, so nothing is held back and nothing can be owed.
  // `replayNow` is a router verdict of "this gesture is what the key already did"
  // (Fired::valid && Fired::replayRawEdge) on this pass.
  Injection onPass(const bool intercepted, const bool replayNow) {
    if (!intercepted) {
      // The key stopped being intercepted. Any owed release is dropped with it —
      // the same bargain Router::configure() makes when a context change resets a
      // detector mid-gesture, and by then the key's own edges flow again.
      releasePending_ = false;
      return {};
    }
    const Injection injection{replayNow, releasePending_};
    releasePending_ = injection.press;
    return injection;
  }

  // True while a release is owed on the next pass.
  bool releasePending() const { return releasePending_; }

  void reset() { releasePending_ = false; }

 private:
  bool releasePending_ = false;
};

}  // namespace button_replay
