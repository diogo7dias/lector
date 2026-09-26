#include <Serialization.h>
#include <gtest/gtest.h>

#include "ReaderRenderSpec.h"

#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)

// Only the services/state used by the production methods; no cache logic here.
struct Section {
  HalFile file;
  std::string filePath = "section.bin";
  uint16_t pageCount = 1;
  bool partial_ = false;
  uint16_t partialPageCount_ = 0;
  uint32_t partialBytesConsumed_ = 0;
  uint32_t partialTotalBytes_ = 0;
  bool cleared = false;
  void writeSectionFileHeader(const ReaderRenderSpec& spec);
  bool loadSectionFile(const ReaderRenderSpec& spec);
  bool clearCache() { return cleared = true; }
};
#include "SectionHeader.inc"

TEST(SectionCacheValidity, OldHeadersRejectedBeforeSpecReadAndNewHeadersRoundTrip) {
  const ReaderRenderSpec spec;
  for (uint8_t oldVersion : {61, 221}) {
    // v61 finalized and suspended headers were 47 bytes, including the retired toggle.
    Storage.bytes.assign(47, 0xFF);
    Storage.bytes[0] = oldVersion;
    Section section;
    EXPECT_FALSE(section.loadSectionFile(spec)) << int(oldVersion);
    EXPECT_EQ(1u, section.file.bytesRead);  // Must not interpret even the first spec field.
    EXPECT_TRUE(section.cleared);
    EXPECT_FALSE(section.file.opened);
  }

  EXPECT_EQ(64, SECTION_FILE_VERSION);
  EXPECT_EQ(218, SECTION_FILE_PARTIAL_VERSION);
  for (uint8_t version : {SECTION_FILE_VERSION, SECTION_FILE_PARTIAL_VERSION}) {
    Section writer;
    writer.writeSectionFileHeader(spec);
    ASSERT_EQ(46u, writer.file.size());
    writer.file.seek(0);
    serialization::writePod(writer.file, version);
    if (version == SECTION_FILE_PARTIAL_VERSION) {
      writer.file.seek(HEADER_SIZE - 2 * sizeof(uint32_t));
      serialization::writePod(writer.file, HEADER_SIZE);                // list-item LUT
      serialization::writePod(writer.file, HEADER_SIZE + uint32_t{4});  // visible-offset LUT
      serialization::writePod(writer.file, uint32_t{0});                // list item
      serialization::writePod(writer.file, uint32_t{0});                // visible offset
      serialization::writePod(writer.file, uint32_t{42});               // consumed bytes
      serialization::writePod(writer.file, uint32_t{100});              // total bytes
    }
    Storage.bytes = writer.file.bytes;
    Section reader;
    ASSERT_TRUE(reader.loadSectionFile(spec)) << int(version);
    EXPECT_EQ(1, reader.pageCount);
    EXPECT_FALSE(reader.cleared);
    EXPECT_EQ(version == SECTION_FILE_PARTIAL_VERSION, reader.partial_);
    if (reader.partial_) {
      EXPECT_EQ(42u, reader.partialBytesConsumed_);
      EXPECT_EQ(100u, reader.partialTotalBytes_);
    }
  }
}
