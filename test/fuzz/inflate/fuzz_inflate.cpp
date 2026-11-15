// libFuzzer harness for InflateReader (raw deflate over uzlib).
//
// Input layout: data[0] picks the API path, data[1] picks read and feed chunk
// sizes, the rest is raw deflate (no zlib or gzip wrapper, InflateReader has none).
//   data[0] & 3 == 0  one-shot, one readAtMost() over the whole output buffer
//   data[0] & 3 == 1  streaming contiguous ring, readAtMost() loop
//   data[0] & 3 == 2  streaming segmented ring, read() loop
//   data[0] & 3 == 3  one-shot, one read() of a sized buffer
// Output is capped at 1 MB per input. Every reader is local to one call.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>

#include "InflateReader.h"

// The vendored uzlib ships without checksum sources; the firmware never calls them
// (same stubs as test/inflate_reader/InflateReaderTest.cpp).
extern "C" uint32_t uzlib_adler32(const void*, unsigned int, uint32_t) { std::abort(); }
extern "C" uint32_t uzlib_crc32(const void*, unsigned int, uint32_t) { std::abort(); }

namespace {

constexpr size_t kMaxOutput = 1024 * 1024;

struct FeedCtx {
  InflateReader reader;  // must be first: the uzlib callback casts uzlib_uncomp* to FeedCtx*
  const uint8_t* src = nullptr;
  size_t remaining = 0;
  size_t chunk = 1;
};

// Hands the decoder at most ctx->chunk input bytes per call.
int feedCb(uzlib_uncomp* u) {
  auto* ctx = reinterpret_cast<FeedCtx*>(u);
  if (ctx->remaining == 0) return -1;
  const size_t n = std::min(ctx->remaining, ctx->chunk);
  const uint8_t* chunk = ctx->src;
  ctx->src += n;
  ctx->remaining -= n;
  u->source = chunk + 1;
  u->source_limit = chunk + n;
  return chunk[0];
}

// Pulls output through readAtMost() until the cap, end of stream, or a stall.
size_t drainAtMost(InflateReader& reader, uint8_t* out, size_t readChunk) {
  size_t total = 0;
  while (total < kMaxOutput) {
    size_t produced = 0;
    const bool ok = reader.readAtMost(out + total, std::min(readChunk, kMaxOutput - total), &produced);
    total += produced;
    if (!ok || produced == 0) break;
  }
  return total;
}

// Pulls output through read() until the cap or the stream ends early.
size_t drainExact(InflateReader& reader, uint8_t* out, size_t readChunk) {
  size_t total = 0;
  while (total < kMaxOutput) {
    const size_t n = std::min(readChunk, kMaxOutput - total);
    if (!reader.read(out + total, n)) break;
    total += n;
  }
  return total;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 2) return 0;

  const uint8_t mode = data[0] & 3;
  const size_t readChunk = 1 + static_cast<size_t>(data[1]) * 16;  // 1..4081 bytes per call
  const size_t feedChunk = 1 + (data[1] & 0x0F);                   // 1..16 input bytes per callback
  const uint8_t* deflate = data + 2;
  const size_t deflateLen = size - 2;

  auto out = std::make_unique<uint8_t[]>(kMaxOutput);

  switch (mode) {
    case 0: {
      InflateReader reader;
      if (!reader.init(false)) return 0;
      reader.setSource(deflate, deflateLen);
      size_t produced = 0;
      reader.readAtMost(out.get(), kMaxOutput, &produced);
      break;
    }
    case 1: {
      FeedCtx ctx;
      if (!ctx.reader.init(true)) return 0;
      ctx.src = deflate;
      ctx.remaining = deflateLen;
      ctx.chunk = feedChunk;
      ctx.reader.setReadCallback(&feedCb);
      drainAtMost(ctx.reader, out.get(), readChunk);
      break;
    }
    case 2: {
      std::unique_ptr<uint8_t[]> owned[InflateReader::RING_SEGMENTS];
      uint8_t* segments[InflateReader::RING_SEGMENTS];
      for (size_t i = 0; i < InflateReader::RING_SEGMENTS; i++) {
        owned[i] = std::make_unique<uint8_t[]>(InflateReader::RING_SEGMENT_BYTES);
        segments[i] = owned[i].get();
      }
      FeedCtx ctx;
      if (!ctx.reader.initWithSegments(segments)) return 0;
      ctx.src = deflate;
      ctx.remaining = deflateLen;
      ctx.chunk = feedChunk;
      ctx.reader.setReadCallback(&feedCb);
      drainExact(ctx.reader, out.get(), readChunk);
      break;
    }
    case 3: {
      InflateReader reader;
      if (!reader.init(false)) return 0;
      reader.setSource(deflate, deflateLen);
      const size_t len = std::min(kMaxOutput, 1 + static_cast<size_t>(data[1]) * 4096);
      reader.read(out.get(), len);
      break;
    }
  }
  return 0;
}
