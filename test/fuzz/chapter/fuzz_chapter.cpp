// libFuzzer harness for ChapterHtmlSlimParser, driven the way SectionBuild.cpp drives it
// (beginParse, parseStep until Done, finishParse), every page serialized like the section
// cache. The ids "c1" and "c2" are TOC chapter boundaries. Input layout:
//   byte 0   render flags (one bit each, see below)
//   byte 1   bits 0-1 font id - 1, bits 2-3 guide dots, bits 4-5 image rendering, bits 6-7 alignment
//   byte 2   read() chunk cap, 0 = whole 1 KB parser buffer
//   byte 3   viewport preset
//   bytes 4.. stylesheet, a NUL, then the chapter XHTML (no NUL: all chapter, no stylesheet)
#include <Epub.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "Epub/Page.h"
#include "Epub/css/CssParser.h"
#include "Epub/parsers/ChapterHtmlSlimParser.h"

namespace {

struct Viewport {
  uint16_t w, h;
};
// Device-real reading areas: X4 portrait and landscape, X3, a narrow column, the dictionary popup.
constexpr Viewport kViewports[] = {{456, 760}, {776, 440}, {500, 760}, {120, 300}, {420, 220}};

void collectPage(void* ctx, std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t) {
  auto& sink = *static_cast<HalFile*>(ctx);
  if (page) page->serialize(sink);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 4) return 0;
  const uint8_t flags = data[0], look = data[1], chunk = data[2];
  const Viewport vp = kViewports[data[3] % std::size(kViewports)];
  const char* rest = reinterpret_cast<const char*>(data + 4);
  const size_t restSize = size - 4;
  const void* nul = memchr(rest, 0, restSize);
  const size_t cssSize = nul ? static_cast<const char*>(nul) - rest : 0;
  const std::string chapterPath = "/book/OEBPS/text/ch.xhtml";

  Storage.reset();
  HalFile::readChunk = SIZE_MAX;
  if (nul) Storage.put("/book/style.css", std::string(rest, cssSize));
  Storage.put(chapterPath, nul ? std::string(rest + cssSize + 1, restSize - cssSize - 1) : std::string(rest, restSize));

  CssParser css("/book/css");
  if (nul) {
    HalFile cssFile;
    if (Storage.openFileForRead("FZ", "/book/style.css", cssFile)) css.loadFromStream(cssFile);
  }

  ReaderRenderSpec spec;
  spec.fontId = 1 + (look & 3);
  spec.lineCompression = (flags & 0x01) ? 0.9f : 1.2f;
  spec.extraParagraphSpacing = flags & 0x02;
  spec.focusReadingEnabled = flags & 0x04;
  spec.embeddedStyle = !(flags & 0x80);
  spec.guideDotsMode = (look >> 2) % 3;
  spec.imageRendering = (look >> 4) % 4 == 3 ? 0 : (look >> 4) % 4;
  spec.paragraphAlignment = (look >> 6) % 4;
  spec.paragraphIndentSpaces = (flags >> 3) % 6;
  spec.characterSpacing = static_cast<int8_t>((flags >> 1) % 5) - 2;
  spec.viewportWidth = vp.w;
  spec.viewportHeight = vp.h;

  HalFile pages;
  Storage.openFileForWrite("FZ", "/book/section.bin", pages);
  Epub epub;
  GfxRenderer renderer;
  ChapterHtmlSlimParser parser(&epub, chapterPath, renderer, spec, &collectPage, &pages, "/book/OEBPS/text/",
                               "/book/img_", {arxHash64("c1"), arxHash64("c2")}, nullptr, nullptr, &css);
  HalFile::readChunk = chunk ? chunk : SIZE_MAX;
  if (parser.beginParse()) {
    ChapterHtmlSlimParser::ParseStatus status;
    while ((status = parser.parseStep()) == ChapterHtmlSlimParser::ParseStatus::More) {
    }
    if (status == ChapterHtmlSlimParser::ParseStatus::Done) {
      parser.finishParse();
    } else {
      parser.abortParse();
    }
  }
  // FUZZ_TRACE=1 prints what each input built, to check seeds reach layout.
  if (getenv("FUZZ_TRACE"))
    fprintf(stderr, "section bytes %zu, anchors %zu\n", pages.size(), parser.getAnchors().size());
  return 0;
}
