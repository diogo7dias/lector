#pragma once

class HalPowerManager {
 public:
  class Lock {
   public:
    Lock() {}  // user-provided, like the real one: no unused-variable warning
  };
  void rearmBatteryLatch() const {}
};

inline HalPowerManager powerManager;
