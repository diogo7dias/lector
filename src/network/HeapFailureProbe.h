#pragma once

#include <cstdint>

/**
 * Records heap allocations that failed, so a failure can say whether it ran out
 * of memory or ran out of server.
 *
 * "Could not reach the font server" is what the font screen says for
 * HttpDownloader::NO_CONNECTION, and NO_CONNECTION is every way a request can
 * end without a status line: a wrong access point, no route, DNS, a refused
 * socket, a TLS alert -- and a handshake that could not allocate what it
 * needed. Those want opposite things done about them and read identically, so
 * the screen has to be able to tell them apart.
 *
 * ESP-IDF calls a registered hook on every failed heap allocation, wherever it
 * comes from: wolfSSL, lwIP, the WiFi driver, or this firmware. The hook only
 * counts and records sizes -- it runs inside the failing allocator, so it must
 * not allocate and must not ask the heap anything. The size of the largest
 * failed allocation is also an upper bound on the largest free block at that
 * moment, which is the number the gate could not see.
 *
 * One transfer at a time: the counters are global, like the flow they measure.
 */
namespace heap_probe {

struct Record {
  uint32_t failures = 0;     // failed allocations since arm()
  uint32_t firstSize = 0;    // bytes the first of them asked for
  uint32_t largestSize = 0;  // bytes the largest of them asked for
};

/** Install the hook (once per run) and clear the counters. */
void arm();

/** What has failed since arm(). */
Record read();

}  // namespace heap_probe
