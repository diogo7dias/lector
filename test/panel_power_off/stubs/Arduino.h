#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

#define PROGMEM
inline uint8_t pgm_read_byte(const uint8_t* p) { return *p; }

constexpr int HIGH = 1;
constexpr int LOW = 0;

inline unsigned long g_fakeMillis = 0;
inline int g_fakeBusy = HIGH;

inline unsigned long millis() { return g_fakeMillis; }
inline unsigned long micros() { return g_fakeMillis * 1000UL; }
inline void delay(unsigned long ms) { g_fakeMillis += ms; }
inline void delayMicroseconds(unsigned long) {}
inline int digitalRead(int) { return g_fakeBusy; }
