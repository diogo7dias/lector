#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
constexpr int HIGH = 1;
constexpr int LOW = 0;
inline int digitalRead(int) { return HIGH; }
inline unsigned long millis() {
  static unsigned long now = 0;
  return ++now;
}
inline unsigned long micros() { return millis() * 1000; }
inline void delay(unsigned long) {}
