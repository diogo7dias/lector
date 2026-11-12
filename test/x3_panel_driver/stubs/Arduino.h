#pragma once
// Just enough Arduino for the X3 panel drivers and EpdBus.h to compile on the host. The
// clock is fake and only moves when a driver sleeps, so the 50 ms BUSY-assert wait costs
// nothing; BUSY reads HIGH (idle) unless a test says otherwise.
#include <cstddef>
#include <cstdint>

#define PROGMEM
constexpr int HIGH = 1;
constexpr int LOW = 0;

inline unsigned long g_fakeMillis = 0;
inline int g_fakeBusy = HIGH;

inline unsigned long millis() { return g_fakeMillis; }
inline unsigned long micros() { return g_fakeMillis * 1000UL; }
inline void delay(unsigned long ms) { g_fakeMillis += ms; }
inline void delayMicroseconds(unsigned long) {}
inline int digitalRead(int) { return g_fakeBusy; }
