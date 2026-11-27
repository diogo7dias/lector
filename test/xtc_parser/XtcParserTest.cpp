#include <HalStorage.h>
#include <XtcParser.h>
#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <memory>
#include <vector>

using namespace xtc;

namespace {

constexpr const char* kPath = "/book.xtc";
constexpr uint16_t kSide = 8;
constexpr uint8_t kFill = 0x55;

// 1-bit pages are row-major ((w+7)/8 * h); 2-bit pages are two column-major planes (w * ceil(h/8) * 2).
uint32_t payloadSize(uint32_t pageMagic) { return pageMagic == XTH_MAGIC ? kSide * ((kSide + 7) / 8) * 2 : 8; }

// One-page book: header, one page table entry, one page.
std::vector<uint8_t> buildBook(uint32_t containerMagic, uint32_t pageMagic) {
  XtcHeader header{};
  header.magic = containerMagic;
  header.versionMajor = 1;
  header.pageCount = 1;
  header.pageTableOffset = sizeof(XtcHeader);
  header.dataOffset = sizeof(XtcHeader) + sizeof(PageTableEntry);

  XtgPageHeader page{};
  page.magic = pageMagic;
  page.width = kSide;
  page.height = kSide;
  page.dataSize = payloadSize(pageMagic);

  PageTableEntry entry{header.dataOffset, static_cast<uint32_t>(sizeof(XtgPageHeader) + page.dataSize), kSide, kSide};

  std::vector<uint8_t> bytes(header.dataOffset + sizeof(XtgPageHeader) + page.dataSize, kFill);
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::memcpy(bytes.data() + header.pageTableOffset, &entry, sizeof(entry));
  std::memcpy(bytes.data() + header.dataOffset, &page, sizeof(page));
  return bytes;
}

PageTableEntry* firstEntry(std::vector<uint8_t>& bytes) {
  return reinterpret_cast<PageTableEntry*>(bytes.data() + sizeof(XtcHeader));
}

struct DepthCase {
  const char* name;
  uint32_t containerMagic;
  uint32_t pageMagic;
  uint8_t expectedDepth;
};

class XtcParserDepth : public ::testing::TestWithParam<DepthCase> {};

// Matching files keep working, and mismatched ones (xtctool writes "XTC\0" around
// "XTH\0" pages) follow the bit depth of their pages.
TEST_P(XtcParserDepth, BitDepthFollowsPagesAndPageLoads) {
  const DepthCase& c = GetParam();
  Storage.setFile(kPath, buildBook(c.containerMagic, c.pageMagic));

  XtcParser parser;
  ASSERT_EQ(parser.open(kPath), XtcError::OK);
  EXPECT_EQ(parser.getBitDepth(), c.expectedDepth);

  PageInfo info{};
  ASSERT_TRUE(parser.getPageInfo(0, info));
  EXPECT_EQ(info.bitDepth, c.expectedDepth);

  const size_t expected = payloadSize(c.pageMagic);
  uint8_t buffer[64]{};
  ASSERT_EQ(parser.loadPage(0, buffer, sizeof(buffer)), expected);
  for (size_t i = 0; i < expected; i++) EXPECT_EQ(buffer[i], kFill) << "byte " << i;

  EXPECT_EQ(parser.loadPage(0, buffer, expected - 1), 0u);
  EXPECT_EQ(parser.getLastError(), XtcError::MEMORY_ERROR);
}

INSTANTIATE_TEST_SUITE_P(Containers, XtcParserDepth,
                         ::testing::Values(DepthCase{"XtcWithXtgPages", XTC_MAGIC, XTG_MAGIC, 1},
                                           DepthCase{"XtchWithXthPages", XTCH_MAGIC, XTH_MAGIC, 2},
                                           DepthCase{"XtcWithXthPages", XTC_MAGIC, XTH_MAGIC, 2},
                                           DepthCase{"XtchWithXtgPages", XTCH_MAGIC, XTG_MAGIC, 1}),
                         [](const ::testing::TestParamInfo<DepthCase>& info) { return info.param.name; });

// An unknown first-page magic does not override the container; the page is still rejected.
TEST(XtcParser, UnknownPageMagicKeepsContainerDepth) {
  for (const uint32_t container : {XTC_MAGIC, XTCH_MAGIC}) {
    Storage.setFile(kPath, buildBook(container, 0x12345678));
    XtcParser parser;
    ASSERT_EQ(parser.open(kPath), XtcError::OK);
    EXPECT_EQ(parser.getBitDepth(), container == XTCH_MAGIC ? 2 : 1);
    uint8_t buffer[64]{};
    EXPECT_EQ(parser.loadPage(0, buffer, sizeof(buffer)), 0u);
    EXPECT_EQ(parser.getLastError(), XtcError::INVALID_MAGIC);
  }
}

// A first-page offset beyond the file (including one that would overflow offset + 4)
// is ignored by the probe: open still succeeds with the container depth, the page fails.
TEST(XtcParser, OutOfRangeFirstPageOffsetKeepsContainerDepth) {
  for (const uint64_t offset : {uint64_t{1} << 40, std::numeric_limits<uint64_t>::max() - 1}) {
    auto bytes = buildBook(XTC_MAGIC, XTH_MAGIC);
    firstEntry(bytes)->dataOffset = offset;
    Storage.setFile(kPath, std::move(bytes));
    XtcParser parser;
    ASSERT_EQ(parser.open(kPath), XtcError::OK);
    EXPECT_EQ(parser.getBitDepth(), 1);
    uint8_t buffer[64]{};
    EXPECT_EQ(parser.loadPage(0, buffer, sizeof(buffer)), 0u);
  }
}

// A first page cut off inside its magic is ignored the same way.
TEST(XtcParser, TruncatedFirstPageKeepsContainerDepth) {
  auto bytes = buildBook(XTC_MAGIC, XTH_MAGIC);
  bytes.resize(sizeof(XtcHeader) + sizeof(PageTableEntry) + 2);
  Storage.setFile(kPath, std::move(bytes));
  XtcParser parser;
  ASSERT_EQ(parser.open(kPath), XtcError::OK);
  EXPECT_EQ(parser.getBitDepth(), 1);
  uint8_t buffer[64]{};
  EXPECT_EQ(parser.loadPage(0, buffer, sizeof(buffer)), 0u);
}

// A 2-bit book whose pages are w x h, payload sized the way loadPage reads it.
std::vector<uint8_t> buildTwoBitBook(uint16_t w, uint16_t h) {
  auto bytes = buildBook(XTCH_MAGIC, XTH_MAGIC);
  const uint32_t packed = ((static_cast<uint32_t>(w) * h + 7) / 8) * 2;
  const size_t pageAt = sizeof(XtcHeader) + sizeof(PageTableEntry);
  bytes.resize(pageAt + sizeof(XtgPageHeader) + packed, kFill);
  firstEntry(bytes)->width = w;
  firstEntry(bytes)->height = h;
  firstEntry(bytes)->dataSize = static_cast<uint32_t>(sizeof(XtgPageHeader) + packed);
  auto* page = reinterpret_cast<XtgPageHeader*>(bytes.data() + pageAt);
  page->width = w;
  page->height = h;
  page->dataSize = packed;
  return bytes;
}

// XtcReaderActivity::renderPage and Xtc::generateCoverBmp size the page buffer as two packed
// planes but index it by padded columns ((h + 7) / 8 bytes each). That only fits when the
// height is a multiple of 8, so any other height must be refused when the book opens.
TEST(XtcParser, TwoBitPageReadStaysInsideItsBuffer) {
  for (const uint16_t h : {8, 16, 12, 799}) {
    Storage.setFile(kPath, buildTwoBitBook(kSide, h));
    XtcParser parser;
    const XtcError err = parser.open(kPath);
    if (h % 8 != 0) {
      EXPECT_EQ(err, XtcError::CORRUPTED_HEADER) << "height " << h;
    }
    if (err != XtcError::OK) continue;
    const uint16_t w = parser.getWidth();
    const uint16_t ph = parser.getHeight();
    const size_t planeSize = (static_cast<size_t>(w) * ph + 7) / 8;
    auto buffer = std::make_unique<uint8_t[]>(planeSize * 2);
    ASSERT_EQ(parser.loadPage(0, buffer.get(), planeSize * 2), planeSize * 2) << "height " << h;
    const size_t colBytes = (ph + 7) / 8;
    unsigned ink = 0;
    for (uint16_t y = 0; y < ph; y++) {
      for (uint16_t x = 0; x < w; x++) {
        const size_t byteOffset = static_cast<size_t>(w - 1 - x) * colBytes + y / 8;
        ink += (buffer[byteOffset] >> (7 - y % 8)) & 1;
        ink += (buffer[planeSize + byteOffset] >> (7 - y % 8)) & 1;
      }
    }
    EXPECT_GT(ink, 0u) << "height " << h;
  }
}

}  // namespace

// A 0-wide or 0-high page passes the 2-bit height check (0 % 8 == 0), then sizes a 0-byte
// page buffer and a 0x0 cover. The book must be refused when it opens.
TEST(XtcParser, ZeroSizedPagesAreRefused) {
  for (const auto& [container, page] : {std::pair{XTC_MAGIC, XTG_MAGIC}, std::pair{XTCH_MAGIC, XTH_MAGIC}}) {
    for (const auto& [w, h] : {std::pair<uint16_t, uint16_t>{0, 0}, {0, kSide}, {kSide, 0}}) {
      auto bytes = buildBook(container, page);
      firstEntry(bytes)->width = w;
      firstEntry(bytes)->height = h;
      Storage.setFile(kPath, bytes);
      XtcParser parser;
      EXPECT_EQ(parser.open(kPath), XtcError::CORRUPTED_HEADER) << w << "x" << h;
    }
  }
}
