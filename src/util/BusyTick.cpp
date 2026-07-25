#include "BusyTick.h"

namespace busy {
namespace {

void (*g_handler)() = nullptr;
void (*g_nowHandler)() = nullptr;

}  // namespace

void tick() {
  if (g_handler != nullptr) g_handler();
}

void tickNow() {
  if (g_nowHandler != nullptr) g_nowHandler();
}

void setTickHandler(void (*handler)(), void (*nowHandler)()) {
  g_handler = handler;
  g_nowHandler = nowHandler;
}

}  // namespace busy
