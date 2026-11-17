// The bench lends no build scratch, so InflateStream takes its heap path as it does
// whenever the firmware's scratch block is absent or already claimed.
#include "BuildScratch.h"

namespace buildscratch {
uint8_t* claim(size_t, size_t*) { return nullptr; }
void release(const uint8_t*) {}
}  // namespace buildscratch
