// A card read that fails mid-chapter returns -1 from HalFile::read (SdFat on an I/O error).
// The parser must treat that as an error or the end of a scan, never as a length.
#include <Epub.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "Epub/Page.h"
#include "Epub/parsers/ChapterHtmlSlimParser.h"

namespace {

void ignorePage(void*, std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t) {}

struct ReadErrorTest : ::testing::Test {
  void TearDown() override { HalFile::readsBeforeError = -1; }
};

// Lays out body, letting the parse make `goodReads` reads after beginParse() before every read fails.
ChapterHtmlSlimParser::ParseStatus layOut(const std::string& body, const int goodReads) {
  const std::string path = "/book/OEBPS/text/ch.xhtml";
  Storage.reset();
  HalFile::readChunk = SIZE_MAX;
  Storage.put(path, "<html><body>" + body + "</body></html>");
  ReaderRenderSpec spec;
  spec.fontId = 1;
  spec.viewportWidth = 456;
  spec.viewportHeight = 760;
  Epub epub;
  GfxRenderer renderer;
  ChapterHtmlSlimParser parser(&epub, path, renderer, spec, &ignorePage, nullptr, "/book/OEBPS/text/", "/book/img_", {},
                               nullptr, nullptr, nullptr);
  EXPECT_TRUE(parser.beginParse());
  HalFile::readsBeforeError = goodReads;
  ChapterHtmlSlimParser::ParseStatus status;
  while ((status = parser.parseStep()) == ChapterHtmlSlimParser::ParseStatus::More) {
  }
  HalFile::readsBeforeError = -1;
  if (status == ChapterHtmlSlimParser::ParseStatus::Done) {
    parser.finishParse();
  } else {
    parser.abortParse();
  }
  return status;
}

}  // namespace

// parseStep() added the -1 to its buffer length and handed expat a buffer of SIZE_MAX bytes.
TEST_F(ReadErrorTest, ChunkReadErrorFailsTheBuild) {
  EXPECT_EQ(layOut("<p>Some text.</p>", 0), ChapterHtmlSlimParser::ParseStatus::Error);
}

// measureTableColumns() walked its 1 KB stack buffer for SIZE_MAX bytes.
TEST_F(ReadErrorTest, TableScanReadErrorStillLaysOutTheTable) {
  EXPECT_EQ(layOut("<table><tr><td>a</td><td>b</td></tr></table><p>After.</p>", 1),
            ChapterHtmlSlimParser::ParseStatus::Done);
}
