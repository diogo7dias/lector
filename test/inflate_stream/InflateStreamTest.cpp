// Host tests for InflateStream (lib/miniz), the tinfl-backed decompressor that
// replaced uzlib on the zip and PNG throughput paths. The fixture holds a
// 40,000-byte repeating-text payload compressed two ways (raw deflate and
// zlib-wrapped, generated with Python's zlib at level 9), exercising real
// back-references far beyond the 258-byte match limit. Locks: one-shot mode,
// streaming ring drain in odd-sized chunks, fill-callback input in small
// slices, zlib header/adler handling, buildscratch claim/release, and
// truncated-stream error reporting.

#include <BuildScratch.h>
#include <InflateStream.h>
#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "InflateStreamFixture.h"

namespace {

// Reproduce the fixture's plaintext: the generator repeated this sentence and
// truncated at kPlainLen bytes.
std::vector<uint8_t> expectedPlain() {
  const std::string unit = "lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
  std::vector<uint8_t> out;
  out.reserve(kPlainLen);
  while (out.size() < kPlainLen) {
    const size_t take = std::min(unit.size(), kPlainLen - out.size());
    out.insert(out.end(), unit.begin(), unit.begin() + take);
  }
  return out;
}

struct SlicedSource {
  const uint8_t* data;
  size_t len;
  size_t pos = 0;
  size_t sliceSize;
};

size_t slicedFill(void* vctx, const uint8_t** data) {
  auto* src = static_cast<SlicedSource*>(vctx);
  if (src->pos >= src->len) return 0;
  const size_t n = std::min(src->sliceSize, src->len - src->pos);
  *data = src->data + src->pos;
  src->pos += n;
  return n;
}

TEST(InflateStream, OneShotRawDeflate) {
  const auto expected = expectedPlain();
  std::vector<uint8_t> out(kPlainLen);

  InflateStream inflate;
  ASSERT_TRUE(inflate.init(false));
  inflate.setSource(kRawDeflate, kRawDeflate_len);
  ASSERT_TRUE(inflate.read(out.data(), out.size()));
  EXPECT_EQ(out, expected);
}

TEST(InflateStream, StreamingChunkedOutput) {
  const auto expected = expectedPlain();
  std::vector<uint8_t> out;
  out.reserve(kPlainLen);

  InflateStream inflate;
  ASSERT_TRUE(inflate.init(true));
  inflate.setSource(kRawDeflate, kRawDeflate_len);

  uint8_t chunk[733];  // odd size, misaligned with the 32KB window on purpose
  while (true) {
    size_t produced = 0;
    const auto status = inflate.readAtMost(chunk, sizeof(chunk), &produced);
    out.insert(out.end(), chunk, chunk + produced);
    ASSERT_NE(status, InflateStream::Status::Error);
    if (status == InflateStream::Status::Done) break;
  }
  EXPECT_EQ(out, expected);
}

TEST(InflateStream, ZlibWrappedViaSmallFillSlices) {
  const auto expected = expectedPlain();
  std::vector<uint8_t> out(kPlainLen);

  SlicedSource src{kZlibWrapped, kZlibWrapped_len, 0, 97};
  InflateStream inflate;
  ASSERT_TRUE(inflate.init(true));
  inflate.setFill(slicedFill, &src);
  inflate.setZlibWrapped();
  ASSERT_TRUE(inflate.read(out.data(), out.size()));
  EXPECT_EQ(out, expected);
}

TEST(InflateStream, TruncatedStreamReportsError) {
  std::vector<uint8_t> out(kPlainLen);

  InflateStream inflate;
  ASSERT_TRUE(inflate.init(true));
  inflate.setSource(kRawDeflate, kRawDeflate_len / 2);  // cut mid-stream
  EXPECT_FALSE(inflate.read(out.data(), out.size()));
}

TEST(InflateStream, ClaimsAndReleasesBuildScratch) {
  // 64KB block comfortably holds tinfl state (~11KB) + 32KB window.
  static uint8_t scratch[64 * 1024];
  buildscratch::lend(scratch, sizeof(scratch));

  {
    const auto expected = expectedPlain();
    std::vector<uint8_t> out(kPlainLen);
    InflateStream inflate;
    ASSERT_TRUE(inflate.init(true));
    // The stream must have claimed the block: a second claimant is refused.
    EXPECT_EQ(buildscratch::claim(1), nullptr);
    inflate.setSource(kRawDeflate, kRawDeflate_len);
    ASSERT_TRUE(inflate.read(out.data(), out.size()));
    EXPECT_EQ(out, expected);
  }
  // Stream destroyed: the block must be claimable again.
  EXPECT_EQ(buildscratch::claim(1), scratch);
  buildscratch::release(scratch);
  buildscratch::reclaim();
}

TEST(InflateStream, HeapFallbackWhenScratchTooSmall) {
  static uint8_t tiny[128];
  buildscratch::lend(tiny, sizeof(tiny));

  const auto expected = expectedPlain();
  std::vector<uint8_t> out(kPlainLen);
  InflateStream inflate;
  ASSERT_TRUE(inflate.init(true));  // must fall back to heap and still work
  inflate.setSource(kRawDeflate, kRawDeflate_len);
  ASSERT_TRUE(inflate.read(out.data(), out.size()));
  EXPECT_EQ(out, expected);

  buildscratch::reclaim();
}

}  // namespace
