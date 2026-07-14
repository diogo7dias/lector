#include "Section.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Serialization.h>

#include "Epub/css/CssParser.h"
#include "Page.h"
#include "hyphenation/Hyphenator.h"
#include "parsers/ChapterHtmlSlimParser.h"

namespace {
// v27: words NFC-composed at layout time; bump invalidates NFD section caches.
// v28: first-line indent px added to the header + cache key.
// v29: word spacing + paragraph spacing added to the header + cache key.
// v30: TextBlock word data stored as one flat arena (offset table + NUL-terminated
//      text blob) instead of length-prefixed strings and per-field arrays.
// v31: TextBlock gains an optional per-word guideDotXOffset array + guideDotsPresent
//      flag (guide dots feature); serialized layout adds one byte per block.
// v32: focus-reading bold length changed (FOCUS_READING_PERCENT 45 -> 43 for CrossInk
//      parity); cached layouts differ, so old sections must regenerate.
constexpr uint8_t SECTION_FILE_VERSION = 32;
// Written into the version byte while a build is in flight. The real version is
// stamped only after every page, LUT and offset has been written (see the commit
// step in createSectionFile), so a build interrupted by a crash or power loss
// leaves a ".part" file whose version (0) loadSectionFile rejects as unknown —
// the section is simply rebuilt, never loaded half-written.
constexpr uint8_t SECTION_FILE_INCOMPLETE_VERSION = 0;
constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(bool) + sizeof(uint8_t) +
                                 sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) + sizeof(bool) +
                                 sizeof(uint8_t) + sizeof(bool) + sizeof(bool) + sizeof(int) + sizeof(uint8_t) +
                                 sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) +
                                 sizeof(uint32_t);
}  // namespace

Section::Section(const std::shared_ptr<Epub>& epub, const int spineIndex, GfxRenderer& renderer)
    : epub(epub),
      spineIndex(spineIndex),
      renderer(renderer),
      filePath(epub->getCachePath() + "/sections/" + std::to_string(spineIndex) + ".bin") {}

// Out-of-line so BuildContext (which holds a unique_ptr to the forward-declared
// ChapterHtmlSlimParser) is a complete type here. Any in-flight build is torn down.
Section::~Section() { abandonBuild(); }

uint32_t Section::onPageComplete(std::unique_ptr<Page> page) {
  if (!file) {
    LOG_ERR("SCT", "File not open for writing page %d", pageCount);
    return 0;
  }

  const uint32_t position = file.position();
  if (!page->serialize(file)) {
    LOG_ERR("SCT", "Failed to serialize page %d", pageCount);
    return 0;
  }
  LOG_DBG("SCT", "Page %d processed", pageCount);

  pageCount++;
  return position;
}

void Section::writeSectionFileHeader(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                                     const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                                     const uint16_t viewportHeight, const bool hyphenationEnabled,
                                     const bool embeddedStyle, const uint8_t imageRendering,
                                     const bool focusReadingEnabled, const bool guideDotsEnabled,
                                     const int firstLineIndentPx, const uint8_t wordSpacing,
                                     const uint8_t paragraphSpacing) {
  if (!file) {
    LOG_DBG("SCT", "File not open for writing header");
    return;
  }
  static_assert(HEADER_SIZE == sizeof(SECTION_FILE_VERSION) + sizeof(fontId) + sizeof(lineCompression) +
                                   sizeof(extraParagraphSpacing) + sizeof(paragraphAlignment) + sizeof(viewportWidth) +
                                   sizeof(viewportHeight) + sizeof(pageCount) + sizeof(hyphenationEnabled) +
                                   sizeof(embeddedStyle) + sizeof(imageRendering) + sizeof(focusReadingEnabled) +
                                   sizeof(guideDotsEnabled) + sizeof(firstLineIndentPx) + sizeof(wordSpacing) +
                                   sizeof(paragraphSpacing) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) +
                                   sizeof(uint32_t),
                "Header size mismatch");
  // Version byte starts as INCOMPLETE; createSectionFile stamps the real version
  // last, once the file is fully written, as the atomic commit point.
  serialization::writePod(file, SECTION_FILE_INCOMPLETE_VERSION);
  serialization::writePod(file, fontId);
  serialization::writePod(file, lineCompression);
  serialization::writePod(file, extraParagraphSpacing);
  serialization::writePod(file, paragraphAlignment);
  serialization::writePod(file, viewportWidth);
  serialization::writePod(file, viewportHeight);
  serialization::writePod(file, hyphenationEnabled);
  serialization::writePod(file, embeddedStyle);
  serialization::writePod(file, imageRendering);
  serialization::writePod(file, focusReadingEnabled);
  serialization::writePod(file, guideDotsEnabled);
  serialization::writePod(file, firstLineIndentPx);
  serialization::writePod(file, wordSpacing);
  serialization::writePod(file, paragraphSpacing);
  serialization::writePod(file, pageCount);  // Placeholder for page count (will be initially 0, patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for LUT offset (patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for anchor map offset (patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for paragraph LUT offset (patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for li LUT offset (patched later)
}

bool Section::loadSectionFile(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                              const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                              const uint16_t viewportHeight, const bool hyphenationEnabled, const bool embeddedStyle,
                              const uint8_t imageRendering, const bool focusReadingEnabled, const bool guideDotsEnabled,
                              const int firstLineIndentPx, const uint8_t wordSpacing, const uint8_t paragraphSpacing) {
  if (!Storage.openFileForRead("SCT", filePath, file)) {
    return false;
  }

  // Match parameters
  {
    uint8_t version;
    serialization::readPod(file, version);
    if (version != SECTION_FILE_VERSION) {
      // Explicit close() required: member variable persists beyond function scope
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Unknown version %u", version);
      clearCache();
      return false;
    }

    int fileFontId;
    uint16_t fileViewportWidth, fileViewportHeight;
    float fileLineCompression;
    bool fileExtraParagraphSpacing;
    uint8_t fileParagraphAlignment;
    bool fileHyphenationEnabled;
    bool fileEmbeddedStyle;
    uint8_t fileImageRendering;
    bool fileFocusReadingEnabled;
    bool fileGuideDotsEnabled;
    int fileFirstLineIndentPx;
    uint8_t fileWordSpacing;
    uint8_t fileParagraphSpacing;
    serialization::readPod(file, fileFontId);
    serialization::readPod(file, fileLineCompression);
    serialization::readPod(file, fileExtraParagraphSpacing);
    serialization::readPod(file, fileParagraphAlignment);
    serialization::readPod(file, fileViewportWidth);
    serialization::readPod(file, fileViewportHeight);
    serialization::readPod(file, fileHyphenationEnabled);
    serialization::readPod(file, fileEmbeddedStyle);
    serialization::readPod(file, fileImageRendering);
    serialization::readPod(file, fileFocusReadingEnabled);
    serialization::readPod(file, fileGuideDotsEnabled);
    serialization::readPod(file, fileFirstLineIndentPx);
    serialization::readPod(file, fileWordSpacing);
    serialization::readPod(file, fileParagraphSpacing);

    if (fontId != fileFontId || lineCompression != fileLineCompression ||
        extraParagraphSpacing != fileExtraParagraphSpacing || paragraphAlignment != fileParagraphAlignment ||
        viewportWidth != fileViewportWidth || viewportHeight != fileViewportHeight ||
        hyphenationEnabled != fileHyphenationEnabled || embeddedStyle != fileEmbeddedStyle ||
        imageRendering != fileImageRendering || focusReadingEnabled != fileFocusReadingEnabled ||
        guideDotsEnabled != fileGuideDotsEnabled || firstLineIndentPx != fileFirstLineIndentPx ||
        wordSpacing != fileWordSpacing || paragraphSpacing != fileParagraphSpacing) {
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Parameters do not match");
      clearCache();
      return false;
    }
  }

  serialization::readPod(file, pageCount);
  // Explicit close() required: member variable persists beyond function scope
  file.close();
  LOG_DBG("SCT", "Deserialization succeeded: %d pages", pageCount);
  return true;
}

// Your updated class method (assuming you are using the 'SD' object, which is a wrapper for a specific filesystem)
bool Section::clearCache() const {
  // Also drop any leftover ".part" from an interrupted build so a stale partial
  // never lingers next to a cleared cache.
  const auto partPath = filePath + ".part";
  if (Storage.exists(partPath.c_str())) {
    Storage.remove(partPath.c_str());
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

bool Section::createSectionFile(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                                const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                                const uint16_t viewportHeight, const bool hyphenationEnabled, const bool embeddedStyle,
                                const uint8_t imageRendering, const bool focusReadingEnabled,
                                const bool guideDotsEnabled, const int firstLineIndentPx, const uint8_t wordSpacing,
                                const uint8_t paragraphSpacing, const std::function<void()>& popupFn) {
  // One-shot: prime the build, then lay out every page and commit in a single call.
  if (!startBuild(fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth, viewportHeight,
                  hyphenationEnabled, embeddedStyle, imageRendering, focusReadingEnabled, guideDotsEnabled,
                  firstLineIndentPx, wordSpacing, paragraphSpacing, popupFn)) {
    return false;
  }
  return buildSomeMore(0);
}

bool Section::startBuild(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                         const uint8_t paragraphAlignment, const uint16_t viewportWidth, const uint16_t viewportHeight,
                         const bool hyphenationEnabled, const bool embeddedStyle, const uint8_t imageRendering,
                         const bool focusReadingEnabled, const bool guideDotsEnabled, const int firstLineIndentPx,
                         const uint8_t wordSpacing, const uint8_t paragraphSpacing,
                         const std::function<void()>& popupFn) {
  // Abandon any prior in-progress build before starting a new one.
  abandonBuild();
  lastBuildLowMemory_ = false;

  const auto localPath = epub->getSpineItem(spineIndex).href;
  const auto tmpHtmlPath = epub->getCachePath() + "/.tmp_" + std::to_string(spineIndex) + ".html";

  // Create cache directory if it doesn't exist
  {
    const auto sectionsDir = epub->getCachePath() + "/sections";
    Storage.mkdir(sectionsDir.c_str());
  }

  // Retry logic for SD card timing issues
  bool success = false;
  uint32_t fileSize = 0;
  for (int attempt = 0; attempt < 3 && !success; attempt++) {
    if (attempt > 0) {
      LOG_DBG("SCT", "Retrying stream (attempt %d)...", attempt + 1);
      delay(50);  // Brief delay before retry
    }

    // Remove any incomplete file from previous attempt before retrying
    if (Storage.exists(tmpHtmlPath.c_str())) {
      Storage.remove(tmpHtmlPath.c_str());
    }

    HalFile tmpHtml;
    if (!Storage.openFileForWrite("SCT", tmpHtmlPath, tmpHtml)) {
      continue;
    }
    // 4KB chunks: transient buffers only live for this call; fewer SD mutex
    // round-trips than 1KB on the cold-build hot path.
    success = epub->readItemContentsToStream(localPath, tmpHtml, 4096);
    fileSize = tmpHtml.size();
    // Explicitly close() file before calling Storage.remove()
    tmpHtml.close();

    // If streaming failed, remove the incomplete file immediately
    if (!success && Storage.exists(tmpHtmlPath.c_str())) {
      Storage.remove(tmpHtmlPath.c_str());
      LOG_DBG("SCT", "Removed incomplete temp file after failed attempt");
    }
  }

  if (!success) {
    LOG_ERR("SCT", "Failed to stream item contents to temp file after retries");
    return false;
  }

  LOG_DBG("SCT", "Streamed temp HTML to %s (%d bytes)", tmpHtmlPath.c_str(), fileSize);

  // Build into a ".part" sibling and atomically rename it over the real cache only
  // once fully written and version-stamped. A crash mid-build leaves the .part (and
  // any previous good .bin) rather than a half-written cache at the canonical path.
  const auto partPath = filePath + ".part";
  if (!Storage.openFileForWrite("SCT", partPath, file)) {
    Storage.remove(tmpHtmlPath.c_str());
    return false;
  }
  writeSectionFileHeader(fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth,
                         viewportHeight, hyphenationEnabled, embeddedStyle, imageRendering, focusReadingEnabled,
                         guideDotsEnabled, firstLineIndentPx, wordSpacing, paragraphSpacing);
  pageCount = 0;
  builtPageCount_ = 0;
  buildComplete_ = false;

  // Derive the content base directory and image cache path prefix for the parser
  const size_t lastSlash = localPath.find_last_of('/');
  const std::string contentBase = (lastSlash != std::string::npos) ? localPath.substr(0, lastSlash + 1) : "";
  const std::string imageBasePath = epub->getCachePath() + "/img_" + std::to_string(spineIndex) + "_";

  CssParser* cssParser = nullptr;
  if (embeddedStyle) {
    cssParser = epub->getCssParser();
    if (cssParser) {
      if (!cssParser->loadFromCache()) {
        LOG_ERR("SCT", "Failed to load CSS from cache");
      }
    }
  }

  // Collect TOC anchors for this spine so the parser can insert page breaks at chapter boundaries
  std::vector<std::string> tocAnchors;
  const int startTocIndex = epub->getTocIndexForSpineIndex(spineIndex);
  if (startTocIndex >= 0) {
    for (int i = startTocIndex; i < epub->getTocItemsCount(); i++) {
      auto entry = epub->getTocItem(i);
      if (entry.spineIndex != spineIndex) break;
      if (!entry.anchor.empty()) {
        tocAnchors.push_back(std::move(entry.anchor));
      }
    }
  }

  // Hold the build state so layout can be driven a page at a time across buildSomeMore()
  // calls. tmpHtmlPath is stored here because the parser keeps a reference to it.
  build_ = makeUniqueNoThrow<BuildContext>();
  if (!build_) {
    LOG_ERR("SCT", "OOM: BuildContext");
    file.close();
    Storage.remove(partPath.c_str());
    Storage.remove(tmpHtmlPath.c_str());
    if (cssParser) cssParser->clear();
    return false;
  }
  build_->tmpHtmlPath = tmpHtmlPath;
  build_->partPath = partPath;
  build_->cssParser = cssParser;

  build_->parser = makeUniqueNoThrow<ChapterHtmlSlimParser>(
      epub, build_->tmpHtmlPath, renderer, fontId, lineCompression, extraParagraphSpacing, paragraphAlignment,
      viewportWidth, viewportHeight, hyphenationEnabled, focusReadingEnabled, guideDotsEnabled, firstLineIndentPx,
      wordSpacing, paragraphSpacing,
      [this](std::unique_ptr<Page> page, const uint16_t paragraphIndex, const uint16_t listItemIndex) {
        if (build_) {
          build_->lut.push_back({onPageComplete(std::move(page)), paragraphIndex, listItemIndex});
          builtPageCount_ = static_cast<uint16_t>(build_->lut.size());
        }
      },
      embeddedStyle, contentBase, imageBasePath, imageRendering, std::move(tocAnchors), popupFn, cssParser);
  if (!build_->parser) {
    LOG_ERR("SCT", "OOM: ChapterHtmlSlimParser");
    abandonBuild();
    return false;
  }

  Hyphenator::setPreferredLanguage(epub->getLanguage());
  if (!build_->parser->beginParse()) {
    LOG_ERR("SCT", "Failed to begin parsing chapter");
    abandonBuild();
    return false;
  }
  return true;
}

bool Section::buildSomeMore(const int maxPages) {
  if (!build_) {
    return buildComplete_;
  }
  const uint16_t startCount = builtPageCount_;
  while (true) {
    // Yield once this call has laid out its budget of pages; the build stays live.
    if (maxPages > 0 && static_cast<int>(builtPageCount_ - startCount) >= maxPages) {
      return true;
    }
    const auto status = build_->parser->parseStep();
    if (status == ChapterHtmlSlimParser::ParseStatus::More) {
      continue;
    }
    if (status == ChapterHtmlSlimParser::ParseStatus::Error) {
      LOG_ERR("SCT", "Parse error during incremental build");
      // Capture the low-memory signal before abandonBuild() tears the parser down,
      // so the caller can tell an OOM abort apart from a parse/IO error.
      if (build_ && build_->parser) {
        lastBuildLowMemory_ = build_->parser->wasLowMemoryAbort();
      }
      abandonBuild();
      return false;
    }
    // Done: flush the trailing page (fires the page callback a final time), then commit.
    build_->parser->finishParse();
    return finalizeBuild();
  }
}

bool Section::finalizeBuild() {
  if (!build_) {
    return false;
  }
  // The unzipped HTML is no longer needed once layout is complete.
  Storage.remove(build_->tmpHtmlPath.c_str());

  // Write the page LUT (byte offset of each serialized page).
  const uint32_t lutOffset = file.position();
  for (const auto& entry : build_->lut) {
    if (entry.fileOffset == 0) {
      LOG_ERR("SCT", "Failed to write LUT due to invalid page positions");
      abandonBuild();
      return false;
    }
    serialization::writePod(file, entry.fileOffset);
  }

  // Write anchor-to-page map for fragment navigation (e.g. footnote targets).
  const uint32_t anchorMapOffset = file.position();
  const auto& anchors = build_->parser->getAnchors();
  serialization::writePod(file, static_cast<uint16_t>(anchors.size()));
  for (const auto& [anchor, page] : anchors) {
    serialization::writeString(file, anchor);
    serialization::writePod(file, page);
  }

  // Paragraph LUT (synthetic XPath p[N] -> page).
  const uint32_t paragraphLutOffset = file.position();
  serialization::writePod(file, static_cast<uint16_t>(build_->lut.size()));
  for (const auto& entry : build_->lut) {
    serialization::writePod(file, entry.paragraphIndex);
  }

  // Li LUT (running list-item index -> page); shares its count with the paragraph LUT.
  const uint32_t liLutFileOffset = static_cast<uint32_t>(file.position());
  for (const auto& entry : build_->lut) {
    serialization::writePod(file, entry.listItemIndex);
  }

  // Patch header with final pageCount and the four trailer offsets.
  file.seek(HEADER_SIZE - sizeof(uint32_t) * 4 - sizeof(pageCount));
  serialization::writePod(file, pageCount);
  serialization::writePod(file, lutOffset);
  serialization::writePod(file, anchorMapOffset);
  serialization::writePod(file, paragraphLutOffset);
  serialization::writePod(file, liLutFileOffset);

  // Commit point: stamp the real version byte LAST, after every offset is patched.
  // A crash before this leaves the .part at INCOMPLETE (0), which loadSectionFile
  // rejects, so a partially written build is never mistaken for a finished cache.
  file.seek(0);
  serialization::writePod(file, SECTION_FILE_VERSION);
  file.flush();
  // Explicit close() required before rename: SdFat cannot rename a path that still
  // has an open FsFile, and the member variable persists beyond function scope.
  file.close();

  if (build_->cssParser) {
    build_->cssParser->clear();
  }

  // Atomically swap the finished build over any previous cache. SdFat's rename does
  // not overwrite, so remove the canonical file first.
  const auto partPath = build_->partPath;
  Storage.remove(filePath.c_str());
  const bool renamed = Storage.rename(partPath.c_str(), filePath.c_str());
  build_.reset();  // tear down parser + in-RAM LUT
  if (!renamed) {
    LOG_ERR("SCT", "Failed to rename section .part into place: %s", filePath.c_str());
    Storage.remove(partPath.c_str());
    return false;
  }
  buildComplete_ = true;
  return true;
}

void Section::abandonBuild() {
  if (!build_) {
    return;
  }
  if (build_->cssParser) {
    build_->cssParser->clear();
  }
  // Tear down the parser's own expat + HTML handle before we touch the files.
  if (build_->parser) {
    build_->parser->abortParse();
  }
  if (file.isOpen()) {
    file.close();
  }
  Storage.remove(build_->partPath.c_str());
  Storage.remove(build_->tmpHtmlPath.c_str());
  build_.reset();
  builtPageCount_ = 0;
  buildComplete_ = false;
}

uint16_t Section::estimatedTotalPages() const {
  if (!build_ || buildComplete_) {
    return pageCount;
  }
  // Building: extrapolate the total from bytes consumed vs. total bytes so far.
  const size_t consumed = build_->parser ? build_->parser->parseBytesConsumed() : 0;
  const size_t total = build_->parser ? build_->parser->parseTotalBytes() : 0;
  if (consumed == 0 || builtPageCount_ == 0) {
    return pageCount;
  }
  uint32_t est = static_cast<uint32_t>(static_cast<uint64_t>(builtPageCount_) * total / consumed);
  if (est < pageCount) est = pageCount;
  if (est > 60000) est = 60000;
  return static_cast<uint16_t>(est);
}

std::unique_ptr<Page> Section::loadPage(const int page) {
  if (build_ && page >= 0 && static_cast<size_t>(page) < build_->lut.size()) {
    // Page already laid out by the active build: read it straight from the .part
    // using the in-RAM offset (the on-disk LUT/trailers are not committed yet).
    if (!Storage.openFileForRead("SCT", build_->partPath, file)) {
      return nullptr;
    }
    file.seek(build_->lut[static_cast<size_t>(page)].fileOffset);
    auto p = Page::deserialize(file);
    file.close();
    return p;
  }
  const int saved = currentPage;
  currentPage = page;
  auto p = loadPageFromSectionFile();
  currentPage = saved;
  return p;
}

std::unique_ptr<Page> Section::loadPageFromSectionFile() {
  if (!Storage.openFileForRead("SCT", filePath, file)) {
    return nullptr;
  }

  file.seek(HEADER_SIZE - sizeof(uint32_t) * 4);
  uint32_t lutOffset;
  serialization::readPod(file, lutOffset);
  file.seek(lutOffset + sizeof(uint32_t) * currentPage);
  uint32_t pagePos;
  serialization::readPod(file, pagePos);
  file.seek(pagePos);

  auto page = Page::deserialize(file);
  // Explicit close() required: member variable persists beyond function scope
  file.close();
  return page;
}

std::string Section::getTextFromSectionFile() {
  std::string fullText;
  auto p = this->loadPageFromSectionFile();
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

  f.seek(HEADER_SIZE - sizeof(uint32_t) * 4 - sizeof(uint16_t));
  uint16_t count;
  serialization::readPod(f, count);
  return count;
}

std::optional<uint16_t> Section::getPageForAnchor(const std::string& anchor) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 3);
  uint32_t anchorMapOffset;
  serialization::readPod(f, anchorMapOffset);
  if (anchorMapOffset == 0 || anchorMapOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(anchorMapOffset);
  uint16_t count;
  serialization::readPod(f, count);
  for (uint16_t i = 0; i < count; i++) {
    std::string key;
    uint16_t page;
    if (!serialization::readString(f, key)) {
      LOG_ERR("SCT", "Invalid anchor string in section cache");
      return std::nullopt;
    }
    serialization::readPod(f, page);
    if (key == anchor) {
      return page;
    }
  }

  return std::nullopt;
}

std::optional<uint16_t> Section::getPageForParagraphIndex(const uint16_t pIndex) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 2);
  uint32_t paragraphLutOffset;
  serialization::readPod(f, paragraphLutOffset);
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset);
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0) {
    return std::nullopt;
  }

  const uint32_t lutEnd = paragraphLutOffset + sizeof(uint16_t) + count * sizeof(uint16_t);
  if (lutEnd > fileSize) {
    return std::nullopt;
  }

  uint16_t resultPage = count - 1;
  for (uint16_t i = 0; i < count; i++) {
    uint16_t pagePIdx;
    serialization::readPod(f, pagePIdx);
    if (pagePIdx >= pIndex) {
      resultPage = i;
      break;
    }
  }

  return resultPage;
}

std::optional<uint16_t> Section::getParagraphIndexForPage(const uint16_t page) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 2);
  uint32_t paragraphLutOffset;
  serialization::readPod(f, paragraphLutOffset);
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset);
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0 || page >= count) {
    return std::nullopt;
  }

  const uint32_t entryEnd = paragraphLutOffset + sizeof(uint16_t) + (page + 1) * sizeof(uint16_t);
  if (entryEnd > fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset + sizeof(uint16_t) + page * sizeof(uint16_t));
  uint16_t pIdx;
  serialization::readPod(f, pIdx);
  return pIdx;
}

std::optional<uint16_t> Section::getPageForListItemIndex(const uint16_t liIndex) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t));
  uint32_t liLutOffset;
  serialization::readPod(f, liLutOffset);
  if (liLutOffset == 0 || liLutOffset >= fileSize) {
    return std::nullopt;
  }

  // The li LUT shares count with the paragraph LUT; read count from paragraphLutOffset
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 2);
  uint32_t paragraphLutOffset;
  serialization::readPod(f, paragraphLutOffset);
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset);
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0) {
    return std::nullopt;
  }

  const uint32_t lutEnd = liLutOffset + count * sizeof(uint16_t);
  if (lutEnd > fileSize) {
    return std::nullopt;
  }

  f.seek(liLutOffset);
  uint16_t resultPage = count - 1;
  for (uint16_t i = 0; i < count; i++) {
    uint16_t pageLiIdx;
    serialization::readPod(f, pageLiIdx);
    if (pageLiIdx >= liIndex) {
      resultPage = i;
      break;
    }
  }

  return resultPage;
}
