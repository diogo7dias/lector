#pragma once

#include <cstddef>

// How the lent framebuffer is divided up for wolfSSL. Kept free of ESP headers
// so the arithmetic can be reasoned about (and tested) on the host.
//
// Two jobs come out of the same block:
//
//  * The large record buffers. A peer that ignores the 2 KB
//    max_fragment_length this firmware asks for sends 16 KB records, and
//    wolfSSL sizes its receive buffer to the record: 16640 bytes plus its own
//    headers, contiguous, held for the session. Two of those can be live at
//    once (handshake buffer, then the first application record), so two fixed
//    slots are reserved for them and nothing else may take one.
//
//  * Everything else wolfSSL asks for -- the session, the decoded certificate,
//    the bignum temporaries -- which is individually small and collectively
//    large. Those used to fall through to the system heap, which on an X3 with
//    WiFi up is where the handshake was left competing for 25544 free bytes in
//    9204-byte pieces. The tail of the block is a heap of its own for them.
namespace tls_scratch {

// One slot. 16640 is the measured ask for a 16 KB record; 17408 covers it with
// 768 bytes over for wolfSSL's own framing. Every byte above that is a byte the
// tail does not get, and the tail is what this layout exists to provide: on the
// smallest framebuffer this firmware runs on (48000 bytes on the X4; the X3's
// is 52272) two 18432-byte slots left the tail at 11136, under the ~16 KB of
// small allocations a handshake was measured making.
constexpr size_t SLOT_BYTES = 17408;
constexpr int NSLOTS = 2;

// At or above this an allocation is a record buffer and takes a slot; below it
// the allocation goes to the tail heap. A 16 KB TLS record asks for 16640.
constexpr size_t MIN_BLOCK_ALLOC = 8192;

// The least the block may be for claim() to take it: both slots plus a tail
// worth having.
constexpr size_t NEEDED = 40 * 1024;

// Bytes left for the tail heap once both slots are carved out of a block of
// `blockLen`. Zero means the block holds the slots and nothing more, which is
// the behaviour this layout replaced.
constexpr size_t tailBytes(const size_t blockLen) {
  return blockLen > static_cast<size_t>(NSLOTS) * SLOT_BYTES ? blockLen - static_cast<size_t>(NSLOTS) * SLOT_BYTES : 0;
}

// Byte offset of the tail heap inside the block.
constexpr size_t tailOffset() { return static_cast<size_t>(NSLOTS) * SLOT_BYTES; }

}  // namespace tls_scratch
