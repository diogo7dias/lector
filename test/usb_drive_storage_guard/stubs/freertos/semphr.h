#pragma once
// Single-threaded host test: the storage mutex only has to exist.
using SemaphoreHandle_t = void*;
constexpr unsigned portMAX_DELAY = ~0u;
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return reinterpret_cast<void*>(1); }
inline int xSemaphoreTakeRecursive(SemaphoreHandle_t, unsigned) { return 1; }
inline int xSemaphoreGiveRecursive(SemaphoreHandle_t) { return 1; }
