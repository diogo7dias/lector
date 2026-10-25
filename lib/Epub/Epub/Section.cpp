#include "Section.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include "Page.h"
#include "SectionFormat.h"
#include "parsers/ChapterHtmlSlimParser.h"

// Out-of-line so the unique_ptr<ChapterHtmlSlimParser> in BuildContext can be
// constructed/destroyed where the parser's full definition is visible.
Section::Section(Epub& epub, const int spineIndex, GfxRenderer& renderer)
    : epub(epub),
      spineIndex(spineIndex),
      renderer(renderer),
      filePath(epub.getCachePath() + "/sections/" + std::to_string(spineIndex) + ".bin") {}

// Suspend any in-progress build so every section.reset() / navigation / sleep path
// persists the pages already laid out as a partial .bin instead of discarding them
// (no-op once a build has completed or never started).
Section::~Section() { suspendBuild(); }

void Section::noteBuildFailure(const BuildFailure code) {
  lastFailure_ = code;
  lastFailureFreeHeap_ = ESP.getFreeHeap();
  lastFailureMaxAlloc_ = ESP.getMaxAllocHeap();
  LOG_ERR("SCT", "Build failure code %d (free heap %u, max alloc %u)", static_cast<int>(code), lastFailureFreeHeap_,
          lastFailureMaxAlloc_);
}

void Section::appendPageToLut(void* const ctx, std::unique_ptr<Page> page, const uint16_t paragraphIndex,
                              const uint16_t listItemIndex, const uint32_t visibleTextOffset) {
  auto* const build = static_cast<BuildContext*>(ctx);
  build->lut.push_back(
      {build->owner->onPageComplete(std::move(page)), paragraphIndex, listItemIndex, visibleTextOffset});
}

uint32_t Section::onPageComplete(std::unique_ptr<Page> page) {
  if (!file) {
    LOG_ERR("SCT", "File not open for writing page %d", builtPageCount_);
    pageWriteFailed_ = true;
    noteBuildFailure(BuildFailure::PageWrite);
    return 0;
  }

  const uint32_t position = file.position();
  if (!page->serialize(file)) {
    LOG_ERR("SCT", "Failed to serialize page %d", builtPageCount_);
    // The parser owns the loop and cannot be stopped from here, so flag it and let
    // buildSomeMore abort on the next step. Grinding on would waste the rest of the
    // chapter and then fail at commit with no clue which page broke.
    pageWriteFailed_ = true;
    noteBuildFailure(BuildFailure::PageWrite);
    return 0;
  }
  LOG_DBG("SCT", "Page %d processed", builtPageCount_);

  builtPageCount_++;
  // pageCount is the pages available to read: a rebuild over a partial only raises it
  // once it has laid out more pages than the partial already covers.
  if (builtPageCount_ > pageCount) {
    pageCount = builtPageCount_;
  }
  return position;
}

void Section::writeSectionFileHeader(const ReaderRenderSpec& spec) {
  if (!file) {
    LOG_DBG("SCT", "File not open for writing header");
    return;
  }
  static_assert(
      HEADER_SIZE == sizeof(SECTION_FILE_VERSION) + sizeof(spec.fontId) + sizeof(spec.lineCompression) +
                         sizeof(spec.extraParagraphSpacing) + sizeof(spec.paragraphSpacing) + sizeof(spec.wordSpacing) +
                         sizeof(spec.paragraphAlignment) + sizeof(spec.viewportWidth) + sizeof(spec.viewportHeight) +
                         sizeof(pageCount) + sizeof(spec.embeddedTextStyle) + sizeof(spec.embeddedLayoutStyle) +
                         sizeof(spec.imageRendering) + sizeof(spec.focusReadingEnabled) + sizeof(spec.guideDotsMode) +
                         sizeof(spec.firstLineIndentMode) + sizeof(spec.firstLineIndentPercent) + sizeof(spec.kerning) +
                         sizeof(spec.ligatures) + sizeof(spec.linkUnderline) + sizeof(spec.bookMargins) +
                         sizeof(spec.headingPageBreak) + sizeof(spec.wordExpansion) + sizeof(uint32_t) +
                         sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t),
      "Header size mismatch");
  // Written as the incomplete sentinel; finalizeBuild() patches it to
  // SECTION_FILE_VERSION as the last step, committing the file.
  serialization::writePod(file, SECTION_FILE_INCOMPLETE_VERSION);
  serialization::writePod(file, spec.fontId);
  serialization::writePod(file, spec.lineCompression);
  serialization::writePod(file, spec.extraParagraphSpacing);
  serialization::writePod(file, spec.paragraphSpacing);
  serialization::writePod(file, spec.wordSpacing);
  serialization::writePod(file, spec.paragraphAlignment);
  serialization::writePod(file, spec.viewportWidth);
  serialization::writePod(file, spec.viewportHeight);
  serialization::writePod(file, spec.embeddedTextStyle);
  serialization::writePod(file, spec.embeddedLayoutStyle);
  serialization::writePod(file, spec.imageRendering);
  serialization::writePod(file, spec.focusReadingEnabled);
  serialization::writePod(file, spec.guideDotsMode);
  serialization::writePod(file, spec.firstLineIndentMode);
  serialization::writePod(file, spec.firstLineIndentPercent);
  serialization::writePod(file, spec.kerning);
  serialization::writePod(file, spec.ligatures);
  serialization::writePod(file, spec.linkUnderline);
  serialization::writePod(file, spec.bookMargins);
  serialization::writePod(file, spec.headingPageBreak);
  serialization::writePod(file, spec.wordExpansion);
  serialization::writePod(file, pageCount);  // Placeholder for page count (will be initially 0, patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for LUT offset (patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for anchor map offset (patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for paragraph LUT offset (patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for li LUT offset (patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for visible-offset LUT (patched later)
}

// The render-spec block of a section header, in file order (writeSectionFileHeader).
static void readSpecFields(HalFile& f, ReaderRenderSpec& out) {
  serialization::readPod(f, out.fontId);
  serialization::readPod(f, out.lineCompression);
  serialization::readPod(f, out.extraParagraphSpacing);
  serialization::readPod(f, out.paragraphSpacing);
  serialization::readPod(f, out.wordSpacing);
  serialization::readPod(f, out.paragraphAlignment);
  serialization::readPod(f, out.viewportWidth);
  serialization::readPod(f, out.viewportHeight);
  serialization::readPod(f, out.embeddedTextStyle);
  serialization::readPod(f, out.embeddedLayoutStyle);
  serialization::readPod(f, out.imageRendering);
  serialization::readPod(f, out.focusReadingEnabled);
  serialization::readPod(f, out.guideDotsMode);
  serialization::readPod(f, out.firstLineIndentMode);
  serialization::readPod(f, out.firstLineIndentPercent);
  serialization::readPod(f, out.kerning);
  serialization::readPod(f, out.ligatures);
  serialization::readPod(f, out.linkUnderline);
  serialization::readPod(f, out.bookMargins);
  serialization::readPod(f, out.headingPageBreak);
  serialization::readPod(f, out.wordExpansion);
}

bool Section::loadSectionFile(const ReaderRenderSpec& spec) {
  if (!Storage.openFileForRead("SCT", filePath, file)) {
    return false;
  }

  // Match parameters
  bool filePartial = false;
  {
    uint8_t version;
    serialization::readPod(file, version);
    if (version != SECTION_FILE_VERSION && version != SECTION_FILE_PARTIAL_VERSION) {
      // Explicit close() required: member variable persists beyond function scope
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Unknown version %u", version);
      clearCache();
      return false;
    }
    filePartial = (version == SECTION_FILE_PARTIAL_VERSION);

    ReaderRenderSpec cached;
    readSpecFields(file, cached);

    if (!sectionCacheMatches(spec, cached)) {
      file.close();
      LOG_DBG("SCT", "Cached layout is for other settings");
      // Toggling a setting back (Justify, then Left, then Justify) lands on the layout
      // kept from last time instead of laying the chapter out again.
      if (swapInPreviousLayout(spec, filePartial)) return loadSectionFile(spec);
      return false;
    }
  }

  serialization::readPod(file, pageCount);

  if (filePartial) {
    // A partial's pageCount is the watermark of a suspended build. Read the watermark
    // trailer (appended after the visible-offset LUT) so estimatedTotalPages can extrapolate.
    uint32_t liLutOffset = 0;
    file.seek(LI_LUT_FIELD);
    serialization::readPod(file, liLutOffset);
    uint32_t visibleLutOffset = 0;
    file.seek(VISIBLE_LUT_FIELD);
    serialization::readPod(file, visibleLutOffset);
    const uint32_t trailerOffset = visibleLutOffset + static_cast<uint32_t>(pageCount) * sizeof(uint32_t);
    const bool trailerValid = pageCount > 0 && liLutOffset >= HEADER_SIZE && visibleLutOffset > liLutOffset &&
                              trailerOffset + 2 * sizeof(uint32_t) <= file.size();
    if (!trailerValid) {
      file.close();
      LOG_ERR("SCT", "Deserialization failed: malformed partial section");
      clearCache();
      pageCount = 0;
      return false;
    }
    file.seek(trailerOffset);
    serialization::readPod(file, partialBytesConsumed_);
    serialization::readPod(file, partialTotalBytes_);
    partial_ = true;
    partialPageCount_ = pageCount;
  }

  // Explicit close() required: member variable persists beyond function scope
  file.close();
  LOG_DBG("SCT", "Deserialization succeeded: %d pages%s", pageCount, filePartial ? " (partial)" : "");
  return true;
}

// Your updated class method (assuming you are using the 'SD' object, which is a wrapper for a specific filesystem)
bool Section::clearCache() const {
  const std::string tmpBin = binTmpPath();
  if (Storage.exists(tmpBin.c_str())) {
    Storage.remove(tmpBin.c_str());
  }
  if (!Storage.exists(filePath.c_str())) {
    LOG_DBG("SCT", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.remove(filePath.c_str())) {
    LOG_ERR("SCT", "Failed to clear cache");
    return false;
  }

  LOG_DBG("SCT", "Cache cleared successfully");
  return true;
}

namespace {

// FAT rename never overwrites, so the destination goes first.
bool replaceFile(const std::string& from, const std::string& to) {
  if (Storage.exists(to.c_str())) Storage.remove(to.c_str());
  return Storage.rename(from.c_str(), to.c_str());
}

}  // namespace

// One layout per chapter is kept beside the live one: the last finished layout made under
// other settings, at <spine>.bin.prev. On a settings mismatch the live file trades places
// with it when it matches, otherwise it becomes it. Only finished layouts are kept: a
// partial is a suspended build, cheap to redo and not worth a slot.
bool Section::swapInPreviousLayout(const ReaderRenderSpec& spec, const bool currentPartial) const {
  const std::string prevPath = filePath + ".prev";
  bool prevMatches = false;
  {
    HalFile prev;
    if (Storage.openFileForRead("SCT", prevPath, prev)) {
      uint8_t version = 0;
      serialization::readPod(prev, version);
      ReaderRenderSpec cached;
      readSpecFields(prev, cached);
      prevMatches = version == SECTION_FILE_VERSION && sectionCacheMatches(spec, cached);
    }
  }

  if (currentPartial) {
    clearCache();
    if (!prevMatches) return false;
    return replaceFile(prevPath, filePath);
  }
  if (!prevMatches) {
    if (!replaceFile(filePath, prevPath)) clearCache();
    return false;
  }
  const std::string swapPath = filePath + ".swap";
  if (!replaceFile(filePath, swapPath) || !replaceFile(prevPath, filePath)) {
    LOG_ERR("SCT", "Layout swap failed");
    return false;
  }
  replaceFile(swapPath, prevPath);
  LOG_DBG("SCT", "Reusing the layout kept for these settings");
  return true;
}

std::unique_ptr<Page> Section::loadPageDuringBuild(const int page) {
  if (!build_ || page < 0 || page >= static_cast<int>(build_->lut.size()) || !file) {
    return nullptr;
  }
  const uint32_t pos = build_->lut[page].fileOffset;
  if (pos == 0) {
    return nullptr;
  }
  // The .bin is open O_RDWR for the build. Read the already-written page, then restore
  // the write cursor so the next onPageComplete keeps appending where it left off.
  const uint32_t writePos = file.position();
  file.seek(pos);
  auto p = Page::deserialize(file);
  file.seek(writePos);
  if (p) {
    p->visibleTextOffset = build_->lut[page].visibleTextOffset;
  }
  return p;
}

// Read a page from the committed file at filePath (finalized section or partial from a
// previous session). Uses a local handle so it is safe while a build holds the member
// `file` open on the tmp .bin.
std::unique_ptr<Page> Section::loadPageAt(const int page) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return nullptr;
  }

  // A torn or damaged cache must yield no page, not a page decoded from a garbage offset.
  const size_t fileSize = f.size();
  uint32_t lutOffset = 0;
  uint32_t pagePos = 0;
  if (!f.seek(PAGE_LUT_FIELD) || !serialization::readPod(f, lutOffset) || lutOffset < HEADER_SIZE ||
      lutOffset > fileSize || lutOffset + sizeof(uint32_t) * (static_cast<size_t>(page) + 1) > fileSize ||
      !f.seek(lutOffset + sizeof(uint32_t) * page) || !serialization::readPod(f, pagePos) || pagePos < HEADER_SIZE ||
      pagePos >= fileSize) {
    LOG_ERR("SCT", "Bad page table entry for page %d in %s", page, filePath.c_str());
    return nullptr;
  }

  // Read this page's visible-codepoint start offset from the visible-offset LUT (last header slot)
  // in the same open handle, so the reader can persist progress without reopening the section file
  // on every page turn (see Page::visibleTextOffset). A malformed/old file leaves it at 0.
  uint32_t visibleLutOffset = 0;
  if (f.seek(VISIBLE_LUT_FIELD)) serialization::readPod(f, visibleLutOffset);
  uint32_t visibleTextOffset = 0;
  const uint32_t visibleEntry = visibleLutOffset + sizeof(uint32_t) * page;
  if (visibleLutOffset >= HEADER_SIZE && visibleEntry + sizeof(uint32_t) <= fileSize) {
    f.seek(visibleEntry);
    serialization::readPod(f, visibleTextOffset);
  }

  f.seek(pagePos);
  auto p = Page::deserialize(f);
  if (p) {
    p->visibleTextOffset = visibleTextOffset;
  }
  return p;
  // No f.close() needed -- DESTRUCTOR_CLOSES_FILE=1 handles it at scope exit
}

std::unique_ptr<Page> Section::loadPage(const int page) {
  if (page < 0) {
    return nullptr;
  }
  if (build_ && page < static_cast<int>(build_->lut.size())) {
    return loadPageDuringBuild(page);
  }
  // Not (yet) in the active build: serve from the file on disk -- a finalized section,
  // or a partial from a previous session whose pages the rebuild hasn't reached again.
  const int onDisk = partial_ ? partialPageCount_ : (build_ ? 0 : pageCount);
  if (page >= onDisk) {
    return nullptr;
  }
  return loadPageAt(page);
}

std::string Section::getTextFromSectionFile() {
  std::string fullText;
  auto p = loadPage(currentPage);
  if (p) {
    for (const auto& el : p->elements) {
      if (el->getTag() == TAG_PageLine) {
        const auto& line = static_cast<const PageLine&>(*el);
        if (line.getBlock()) {
          const auto& block = *line.getBlock();
          for (uint16_t i = 0; i < block.wordCount(); i++) {
            if (!fullText.empty()) fullText += " ";
            fullText += block.wordText(i);
          }
        }
      }
    }
  }
  return fullText;
}

std::optional<uint16_t> Section::getCachedPageCount() const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  if (fileSize < HEADER_SIZE) {
    return std::nullopt;
  }

  // Only a finalized section's count is the chapter total; a partial's count is just the
  // suspended build's watermark, which would skew progress mapping. Callers fall back to
  // their own estimates.
  uint8_t version;
  serialization::readPod(f, version);
  if (version != SECTION_FILE_VERSION) {
    return std::nullopt;
  }

  f.seek(PAGE_COUNT_FIELD);
  uint16_t count;
  serialization::readPod(f, count);
  return count;
}

bool Section::openAnchorMap(HalFile& f, uint16_t& count) const {
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return false;
  }

  const uint32_t fileSize = f.size();
  f.seek(ANCHOR_MAP_FIELD);
  uint32_t anchorMapOffset;
  serialization::readPod(f, anchorMapOffset);
  if (anchorMapOffset == 0 || anchorMapOffset >= fileSize) {
    return false;
  }
  f.seek(anchorMapOffset);
  return serialization::readPod(f, count);
}

std::optional<uint16_t> Section::getPageForAnchor(const std::string& anchor) const {
  HalFile f;
  uint16_t count;
  if (!openAnchorMap(f, count)) {
    return std::nullopt;
  }

  // Fixed-width keys, so the scan allocates nothing per entry — the old string map built
  // and threw away one std::string for every anchor it walked past.
  const uint64_t targetHash = arxHash64(anchor);
  for (uint16_t i = 0; i < count; i++) {
    uint64_t key;
    uint16_t page;
    serialization::readPod(f, key);
    serialization::readPod(f, page);
    if (key == targetHash) {
      return page;
    }
  }

  return std::nullopt;
}
