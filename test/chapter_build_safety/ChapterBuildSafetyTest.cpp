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

ReaderRenderSpec defaultSpec() {
  ReaderRenderSpec spec;
  spec.fontId = 1;
  spec.lineCompression = 1.1f;
  spec.viewportWidth = 456;
  spec.viewportHeight = 760;
  spec.imageRendering = 0;  // display images
  return spec;
}

}  // namespace

// Image fallback extraction fires popupFn from parseStep() (Section::buildSomeMore), not
// from beginParse() (Section::startBuild), so popupCtx must stay valid for every later step.
TEST(ChapterBuildSafety, ImageFallbackFiresPopupDuringParseStep) {
  Storage.reset();
  HalFile::readChunk = SIZE_MAX;

  const std::string chapterPath = "/book/OEBPS/text/ch.xhtml";
  // The stub Epub only emits valid PNG headers for .png hrefs; .jpg returns false,
  // exercising the fallback extraction path in ChapterHtmlSlimParser::startElement().
  Storage.put(chapterPath, "<html><body><p>Before</p><img src=\"broken.jpg\"/><p>After</p></body></html>");

  int popupCalls = 0;
  const PopupFn popupFn = [](void* ctx) { (*static_cast<int*>(ctx))++; };

  Epub epub;
  GfxRenderer renderer;
  ChapterHtmlSlimParser parser(&epub, chapterPath, renderer, defaultSpec(), &ignorePage, nullptr, "/book/OEBPS/text/",
                               "/book/img_", {}, popupFn, &popupCalls, nullptr);

  ASSERT_TRUE(parser.beginParse());
  EXPECT_EQ(popupCalls, 0);

  ChapterHtmlSlimParser::ParseStatus status;
  while ((status = parser.parseStep()) == ChapterHtmlSlimParser::ParseStatus::More) {
  }
  ASSERT_EQ(status, ChapterHtmlSlimParser::ParseStatus::Done);
  parser.finishParse();

  EXPECT_EQ(popupCalls, 1);
}

// Once the blocking phase ends, clearPopupCallback() must prevent background parseStep()
// ticks from invoking popupFn or touching its context pointer.
TEST(ChapterBuildSafety, ClearPopupCallbackDisarmsPopupForBackgroundSteps) {
  Storage.reset();
  HalFile::readChunk = SIZE_MAX;

  const std::string chapterPath = "/book/OEBPS/text/ch.xhtml";
  Storage.put(chapterPath, "<html><body><p>Before</p><img src=\"broken.jpg\"/><p>After</p></body></html>");

  int popupCalls = 0;
  const PopupFn popupFn = [](void* ctx) { (*static_cast<int*>(ctx))++; };

  Epub epub;
  GfxRenderer renderer;
  ChapterHtmlSlimParser parser(&epub, chapterPath, renderer, defaultSpec(), &ignorePage, nullptr, "/book/OEBPS/text/",
                               "/book/img_", {}, popupFn, &popupCalls, nullptr);

  ASSERT_TRUE(parser.beginParse());
  parser.clearPopupCallback();

  ChapterHtmlSlimParser::ParseStatus status;
  while ((status = parser.parseStep()) == ChapterHtmlSlimParser::ParseStatus::More) {
  }
  ASSERT_EQ(status, ChapterHtmlSlimParser::ParseStatus::Done);
  parser.finishParse();

  EXPECT_EQ(popupCalls, 0);
}
