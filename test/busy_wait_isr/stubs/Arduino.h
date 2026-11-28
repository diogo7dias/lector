#pragma once
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define IRAM_ATTR
#define DRAM_ATTR

constexpr int HIGH = 1;
constexpr int LOW = 0;
constexpr int INPUT = 0;
constexpr int INPUT_PULLUP = 1;
constexpr int OUTPUT = 2;
constexpr int RISING = 1;
constexpr int FALLING = 2;
constexpr int CHANGE = 3;

struct FakeSerial {
  explicit operator bool() const { return false; }
  void printf(const char*, ...) const {}
};
inline FakeSerial Serial;

struct FakeArduinoState {
  unsigned long nowMs = 0;
  int busyLevel = HIGH;
  int delayCalls = 0;
  int attachCount = 0;
  int detachCount = 0;
  int lastAttachMode = -1;
  void (*attachedIsr)() = nullptr;
  void (*onAttach)() = nullptr;
  void (*onDelay)(unsigned long ms) = nullptr;
};
inline FakeArduinoState g_hw;

inline unsigned long millis() { return g_hw.nowMs; }
inline unsigned long micros() { return g_hw.nowMs * 1000UL; }
inline void delay(unsigned long ms) {
  g_hw.nowMs += ms;
  g_hw.delayCalls++;
  if (g_hw.onDelay) g_hw.onDelay(ms);
}
inline void delayMicroseconds(unsigned long) {}
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return g_hw.busyLevel; }
inline int digitalPinToInterrupt(int pin) { return pin; }
inline void attachInterrupt(int, void (*isr)(), int mode) {
  g_hw.attachCount++;
  g_hw.lastAttachMode = mode;
  g_hw.attachedIsr = isr;
  if (g_hw.onAttach) g_hw.onAttach();
}
inline void detachInterrupt(int) {
  g_hw.detachCount++;
  g_hw.attachedIsr = nullptr;
}
