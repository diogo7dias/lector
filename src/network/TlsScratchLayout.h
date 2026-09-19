#pragma once

#include <cstddef>

// How the lent framebuffer is divided up for wolfSSL. Kept free of ESP headers
// so the arithmetic can be reasoned about (and tested) on the host.
//
// There is no division any more, and that is the point. The block used to be
// carved into two fixed 17408-byte slots for the large record buffers plus a
// tail heap for everything else. The X3 log of 2026-09-19 measured what that
// cost: "Scratch tail low-water 32 of 17456 bytes; 33 allocations fell back to
// the heap (largest 5368)". The small allocations had 17456 bytes, used all but
// 32 of them, and spilled 33 times onto a system heap holding 17820 fragmented
// bytes -- where a 5368-byte ask came back null twice and the handshake died
// with PEER_KEY_ERROR (-342) / MP_EXPTMOD_E (-112), wolfSSL's names for "the
// peer's key could not be decoded" when the buffer behind the decode was never
// allocated.
//
// Meanwhile the slots were a standing reservation: 34816 bytes that only one
// record buffer occupies outside the brief window where GrowInputBuffer fills a
// new buffer before freeing the old. One heap over the whole block hands that
// idle slot to the small allocations and keeps the record buffers served from
// the same bytes:
//
//                         old tail   new pool, one record live   two live
//   X3 (52272 bytes)         17456                       35632      18992
//   X4 (48000 bytes)         13184                       31360      14720
//
// Strictly more room in every state, which is what "do not break the handshake
// that already works" requires.
namespace tls_scratch {

// What wolfSSL asks for when a peer ignores the 2 KB max_fragment_length this
// firmware requests and sends a 16 KB record: one contiguous buffer, sized to
// the record and held for the session.
constexpr size_t RECORD_BYTES = 16640;

// Two record buffers can be live at once. GrowInputBuffer allocates the larger
// buffer, copies into it, and only then frees the old one.
constexpr int MAX_LIVE_RECORDS = 2;

// The least the block may be for claim() to take it: both record buffers plus a
// small-allocation pool worth having.
constexpr size_t NEEDED = 40 * 1024;

// Bytes left for everything that is not a record buffer while both record
// buffers are live -- the worst moment in a handshake, and the figure the old
// fixed tail was stuck at permanently.
constexpr size_t smallPoolBytes(const size_t blockLen) {
  constexpr size_t records = static_cast<size_t>(MAX_LIVE_RECORDS) * RECORD_BYTES;
  return blockLen > records ? blockLen - records : 0;
}

}  // namespace tls_scratch
