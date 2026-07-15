#pragma once

// Host-test stub: Serialization.h probes the largest free block before big
// string reserves. On the host, pretend the heap is huge.

#include <cstddef>

#define MALLOC_CAP_DEFAULT 0

inline size_t heap_caps_get_largest_free_block(int /*caps*/) { return 1024u * 1024u * 64u; }
