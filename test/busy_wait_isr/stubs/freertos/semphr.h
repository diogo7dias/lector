#pragma once
#include "Arduino.h"
#include "FreeRTOS.h"

struct FakeSem {
  bool given = false;
};
using SemaphoreHandle_t = FakeSem*;

struct FakeSemState {
  FakeSem sem{};
  int blockingTakes = 0;
  BaseType_t (*onBlockingTake)(TickType_t ticks) = nullptr;
};
inline FakeSemState g_sem;

inline SemaphoreHandle_t xSemaphoreCreateBinary() {
  g_sem.sem.given = false;
  return &g_sem.sem;
}

inline BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t* woken) {
  s->given = true;
  if (woken) *woken = pdTRUE;
  return pdTRUE;
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks) {
  if (s->given) {
    s->given = false;
    return pdTRUE;
  }
  if (ticks == 0) return pdFALSE;
  g_sem.blockingTakes++;
  if (g_sem.onBlockingTake) return g_sem.onBlockingTake(ticks);
  return pdFALSE;
}
