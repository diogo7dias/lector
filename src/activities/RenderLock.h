#pragma once

class Activity;  // forward declaration

// RAII helper to lock rendering mutex for the duration of a scope.
class RenderLock {
  bool isLocked = false;

 public:
  explicit RenderLock();
  explicit RenderLock(Activity&);  // same lock as the default constructor; the Activity is not used
  // Takes the lock only if it is free right now; owns() says whether it did.
  struct TryOnly {};
  explicit RenderLock(TryOnly);
  bool owns() const { return isLocked; }
  // True when the calling task already holds the lock (taking it again would deadlock).
  static bool heldByCurrentTask();
  RenderLock(const RenderLock&) = delete;
  RenderLock& operator=(const RenderLock&) = delete;
  ~RenderLock();
  void unlock();
  static bool peek();
};
