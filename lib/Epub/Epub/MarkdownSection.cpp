#include "MarkdownSection.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include "Page.h"

namespace {
constexpr uint32_t MD_SECTION_MAGIC = 0x3153444D;  // "MDS1"
constexpr uint8_t MD_SECTION_VERSION = 1;

void writeSpec(HalFile& f, const ReaderRenderSpec& s) {
  serialization::writePod(f, s.fontId);
  serialization::writePod(f, s.lineCompression);
  serialization::writePod(f, s.extraParagraphSpacing);
  serialization::writePod(f, s.wordSpacing);
  serialization::writePod(f, s.paragraphAlignment);
  serialization::writePod(f, s.viewportWidth);
  serialization::writePod(f, s.viewportHeight);
  serialization::writePod(f, s.embeddedStyle);
  serialization::writePod(f, s.imageRendering);
  serialization::writePod(f, s.focusReadingEnabled);
  serialization::writePod(f, s.guideDotsMode);
  serialization::writePod(f, s.paragraphIndentSpaces);
  serialization::writePod(f, s.characterSpacing);
}

bool readSpec(HalFile& f, ReaderRenderSpec& out) {
  return serialization::readPod(f, out.fontId) && serialization::readPod(f, out.lineCompression) &&
         serialization::readPod(f, out.extraParagraphSpacing) && serialization::readPod(f, out.wordSpacing) &&
         serialization::readPod(f, out.paragraphAlignment) && serialization::readPod(f, out.viewportWidth) &&
         serialization::readPod(f, out.viewportHeight) && serialization::readPod(f, out.embeddedStyle) &&
         serialization::readPod(f, out.imageRendering) && serialization::readPod(f, out.focusReadingEnabled) &&
         serialization::readPod(f, out.guideDotsMode) && serialization::readPod(f, out.paragraphIndentSpaces) &&
         serialization::readPod(f, out.characterSpacing);
}

constexpr uint32_t HEADER_SIZE =
    sizeof(uint32_t) /*magic*/ + sizeof(uint8_t) /*version*/ + sizeof(uint32_t) /*fileSize*/ + sizeof(int) /*fontId*/ +
    sizeof(float) /*lineCompression*/ + sizeof(bool) /*extraParagraphSpacing*/ + sizeof(uint8_t) /*wordSpacing*/ +
    sizeof(uint8_t) /*alignment*/ + sizeof(uint16_t) * 2 /*viewport*/ + sizeof(bool) /*embeddedStyle*/ +
    sizeof(uint8_t) /*imageRendering*/ + sizeof(bool) /*focusReading*/ + sizeof(uint8_t) /*guideDotsMode*/ +
    sizeof(uint8_t) /*paragraphIndentSpaces*/ + sizeof(int8_t) /*characterSpacing*/ + sizeof(uint16_t) /*pageCount*/ +
    sizeof(uint16_t) /*tocCount*/ + sizeof(uint32_t) * 2 /*pageLutOffset, tocOffset*/;

constexpr uint32_t FOOTER_FIELDS_OFFSET = HEADER_SIZE - (sizeof(uint16_t) * 2 + sizeof(uint32_t) * 2);

struct BuildSink {
  HalFile* file = nullptr;
  std::vector<uint32_t> pageOffsets;
  bool writeFailed = false;
};

void onMarkdownPage(void* const ctx, std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t) {
  auto* sink = static_cast<BuildSink*>(ctx);
  if (!page || !sink || sink->writeFailed) return;
  const uint32_t pos = static_cast<uint32_t>(sink->file->position());
  if (!page->serialize(*sink->file)) {
    sink->writeFailed = true;
    return;
  }
  sink->pageOffsets.push_back(pos);
}

}  // namespace

MarkdownSection::MarkdownSection(std::string cacheDir) : filePath_(std::move(cacheDir) + "/section.bin") {}

bool MarkdownSection::load(const ReaderRenderSpec& spec, const uint32_t fileSize) {
  pageCount_ = 0;
  pageLutOffset_ = 0;
  toc_.clear();

  HalFile f;
  if (!Storage.openFileForRead("MDS", filePath_, f)) return false;
  if (f.size() < HEADER_SIZE) return false;

  uint32_t magic = 0;
  uint8_t version = 0;
  uint32_t cachedSize = 0;
  ReaderRenderSpec cachedSpec;
  if (!serialization::readPod(f, magic) || magic != MD_SECTION_MAGIC) return false;
  if (!serialization::readPod(f, version) || version != MD_SECTION_VERSION) return false;
  if (!serialization::readPod(f, cachedSize) || cachedSize != fileSize) return false;
  if (!readSpec(f, cachedSpec) || !sectionCacheMatches(spec, cachedSpec)) return false;

  uint16_t pages = 0;
  uint16_t tocCount = 0;
  uint32_t lutOff = 0;
  uint32_t tocOff = 0;
  if (!serialization::readPod(f, pages) || !serialization::readPod(f, tocCount) || !serialization::readPod(f, lutOff) ||
      !serialization::readPod(f, tocOff)) {
    return false;
  }
  if (pages == 0 || lutOff < HEADER_SIZE || tocOff < lutOff) return false;
  if (static_cast<uint64_t>(lutOff) + static_cast<uint64_t>(pages) * sizeof(uint32_t) > f.size()) return false;
  if (tocCount > MarkdownParser::MAX_TOC_ENTRIES || tocOff > f.size()) return false;

  if (tocCount > 0) {
    if (!f.seek(tocOff)) return false;
    toc_.reserve(tocCount);
    for (uint16_t i = 0; i < tocCount; ++i) {
      MarkdownTocEntry entry;
      uint16_t titleLen = 0;
      if (!serialization::readPod(f, entry.pageIndex) || !serialization::readPod(f, entry.level) ||
          !serialization::readPod(f, titleLen) || titleLen > 256) {
        toc_.clear();
        return false;
      }
      entry.title.resize(titleLen);
      if (titleLen > 0 && f.read(entry.title.data(), titleLen) != titleLen) {
        toc_.clear();
        return false;
      }
      toc_.push_back(std::move(entry));
    }
  }

  pageCount_ = pages;
  pageLutOffset_ = lutOff;
  return true;
}

bool MarkdownSection::build(const std::string& mdPath, GfxRenderer& renderer, const ReaderRenderSpec& spec,
                            const uint32_t fileSize) {
  pageCount_ = 0;
  pageLutOffset_ = 0;
  toc_.clear();

  const std::string partPath = FsHelpers::partialPathFor(filePath_);
  if (Storage.exists(partPath.c_str())) Storage.remove(partPath.c_str());

  bool ok = false;
  {
    HalFile out;
    if (!Storage.openFileForWrite("MDS", partPath, out)) return false;

    serialization::writePod(out, static_cast<uint32_t>(0));  // magic patched on commit
    serialization::writePod(out, MD_SECTION_VERSION);
    serialization::writePod(out, fileSize);
    writeSpec(out, spec);
    serialization::writePod(out, uint16_t{0});  // pageCount
    serialization::writePod(out, uint16_t{0});  // tocCount
    serialization::writePod(out, uint32_t{0});  // pageLutOffset
    serialization::writePod(out, uint32_t{0});  // tocOffset

    BuildSink sink;
    sink.file = &out;
    MarkdownParser parser(mdPath, renderer, spec, &onMarkdownPage, &sink);
    if (!parser.parseAndBuildPages() || sink.writeFailed || sink.pageOffsets.empty()) {
      out.close();
      Storage.remove(partPath.c_str());
      return false;
    }

    const uint32_t lutOff = static_cast<uint32_t>(out.position());
    for (const uint32_t off : sink.pageOffsets) serialization::writePod(out, off);

    const uint32_t tocOff = static_cast<uint32_t>(out.position());
    const auto& parsedToc = parser.getToc();
    for (const auto& entry : parsedToc) {
      const uint16_t len = static_cast<uint16_t>(std::min<size_t>(entry.title.size(), 256));
      serialization::writePod(out, entry.pageIndex);
      serialization::writePod(out, entry.level);
      serialization::writePod(out, len);
      if (len > 0) out.write(entry.title.data(), len);
    }

    const auto pages = static_cast<uint16_t>(std::min<size_t>(sink.pageOffsets.size(), UINT16_MAX));
    const auto tocCount = static_cast<uint16_t>(std::min<size_t>(parsedToc.size(), MarkdownParser::MAX_TOC_ENTRIES));
    out.seek(FOOTER_FIELDS_OFFSET);
    serialization::writePod(out, pages);
    serialization::writePod(out, tocCount);
    serialization::writePod(out, lutOff);
    serialization::writePod(out, tocOff);
    out.seek(0);
    serialization::writePod(out, MD_SECTION_MAGIC);

    pageCount_ = pages;
    pageLutOffset_ = lutOff;
    toc_ = parsedToc;
    ok = true;
  }

  if (!ok) {
    Storage.remove(partPath.c_str());
    return false;
  }
  if (Storage.exists(filePath_.c_str())) Storage.remove(filePath_.c_str());
  if (!Storage.rename(partPath.c_str(), filePath_.c_str())) {
    Storage.remove(partPath.c_str());
    pageCount_ = 0;
    toc_.clear();
    return false;
  }
  return true;
}

std::unique_ptr<Page> MarkdownSection::loadPage(const int pageIndex) const {
  if (pageIndex < 0 || pageIndex >= pageCount_ || pageLutOffset_ == 0) return nullptr;
  HalFile f;
  if (!Storage.openFileForRead("MDS", filePath_, f)) return nullptr;
  const uint32_t entryPos = pageLutOffset_ + static_cast<uint32_t>(pageIndex) * sizeof(uint32_t);
  uint32_t pageOffset = 0;
  if (!f.seek(entryPos) || !serialization::readPod(f, pageOffset) || !f.seek(pageOffset)) return nullptr;
  return Page::deserialize(f, "");
}
