// When the header probe cannot size an image, the chapter parser extracts the whole image to
// the book cache. Everything that later reads that cache trusts a file that exists, so a
// reset in the middle of that write must leave nothing cut under the final name.
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "Epub/Page.h"
#include "Epub/parsers/ChapterHtmlSlimParser.h"

namespace {

constexpr const char* kCached = "/book/img_0.jpg";

void ignorePage(void*, std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t) {}

// The cached image as the card holds it at the moment the stream is cut: what a reset there leaves.
std::string atCut;
bool cutSeen = false;

void snapshotAtCut() {
  cutSeen = true;
  HalFile file;
  atCut = "<none>";
  if (!Storage.openFileForRead("T", kCached, file)) return;
  atCut.assign(file.size(), '\0');
  file.read(atCut.data(), atCut.size());
}

void layOut(const std::string& body) {
  const std::string path = "/book/OEBPS/text/ch.xhtml";
  Storage.put(path, "<html><body>" + body + "</body></html>");
  HalFile::readChunk = SIZE_MAX;
  ReaderRenderSpec spec;
  spec.fontId = 1;
  spec.viewportWidth = 456;
  spec.viewportHeight = 760;
  Epub epub;
  GfxRenderer renderer;
  ChapterHtmlSlimParser parser(&epub, path, renderer, spec, &ignorePage, nullptr, "/book/OEBPS/text/", "/book/img_", {},
                               nullptr, nullptr, nullptr);
  ASSERT_TRUE(parser.beginParse());
  ChapterHtmlSlimParser::ParseStatus status;
  while ((status = parser.parseStep()) == ChapterHtmlSlimParser::ParseStatus::More) {
  }
  ASSERT_EQ(status, ChapterHtmlSlimParser::ParseStatus::Done);
  parser.finishParse();
}

struct ImageFallbackCache : ::testing::Test {
  void SetUp() override {
    Storage.reset();
    cutSeen = false;
    Epub::midStream = &snapshotAtCut;
  }
  void TearDown() override { Epub::midStream = nullptr; }
};

}  // namespace

// A reset mid-extraction leaves no cut image under the final name, and the failed run cleans
// up its staging file.
TEST_F(ImageFallbackCache, ACutExtractNeverShowsUnderTheFinalName) {
  layOut("<p>Before</p><img src=\"cut.jpg\"/><p>After</p>");
  ASSERT_TRUE(cutSeen);
  EXPECT_EQ(atCut, "<none>");
  EXPECT_FALSE(Storage.exists(kCached));
  EXPECT_FALSE(Storage.exists(FsHelpers::partialPathFor(kCached).c_str()));
}

// A rebuild over an older cache maps the same image to the same name: a reset mid-extraction
// leaves that older whole file as it was, never cut down to the bytes that arrived.
TEST_F(ImageFallbackCache, ACutExtractLeavesAnOlderWholeImageWhole) {
  Storage.put(kCached, "old bytes");
  layOut("<p>Before</p><img src=\"cut.jpg\"/><p>After</p>");
  ASSERT_TRUE(cutSeen);
  EXPECT_EQ(atCut, "old bytes");
}
