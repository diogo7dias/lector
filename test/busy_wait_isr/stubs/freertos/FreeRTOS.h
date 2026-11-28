#pragma once
#include <cstdint>

using BaseType_t = int;
using TickType_t = uint32_t;
constexpr BaseType_t pdFALSE = 0;
constexpr BaseType_t pdTRUE = 1;

inline TickType_t pdMS_TO_TICKS(unsigned long ms) { return static_cast<TickType_t>(ms); }
inline void portYIELD_FROM_ISR() {}
