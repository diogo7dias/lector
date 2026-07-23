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
// v33: force-split long runs (CJK) now mark word continuation (upstream #2652), so
//      the wrapped layout differs; old sections must regenerate.
// v34: layout inputs consolidated into LayoutParams; the generation hash now also
//      folds CssParser::CSS_CACHE_VERSION, so a CSS-engine change invalidates section
//      caches too (previously only book.bin's css_rules.cache was versioned). The
//      on-disk field order is unchanged; the bump forces the one-time re-index.
constexpr uint8_t SECTION_FILE_VERSION = 36;  // 36: paragraphOrdinal skips headings
// Written into the version byte while a build is in flight. The real version is
// stamped only after every page, LUT and offset has been written (see the commit
// step in createSectionFile), so a build interrupted by a crash or power loss
// leaves a ".part" file whose version (0) loadSectionFile rejects as unknown —
// the section is simply rebuilt, never loaded half-written.
constexpr uint8_t SECTION_FILE_INCOMPLETE_VERSION = 0;
// Start small: most chapters fit well under 128 pages, and the LUT doubles on
// demand (ensurePageLutCapacity). 1024 up-front cost 8KB of heap for every
// chapter build — held across the whole build, exactly when the heap is most
// fragmented. 128 entries = 1KB; a genuinely huge chapter pays a few doubling
// copies instead.
constexpr uint16_t INITIAL_SECTION_PAGE_LUT_ENTRIES = 128;
constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(bool) + sizeof(uint8_t) +
                                 sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) + sizeof(bool) +
                                 sizeof(uint8_t) + sizeof(bool) + sizeof(bool) + sizeof(int) + sizeof(uint8_t) +
                                 sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) +
                                 sizeof(uint32_t);

// ---- Layout-settings cache generations -------------------------------------
// Section caches (and their per-chapter image files) live under
// "<cachePath>/sections/<hash8>/", where hash8 is an FNV-1a fold of every
// layout-affecting render setting. A settings change lands in a fresh
// directory; the outgoing generation is kept so toggling the setting back
// reuses its already-built pages. Only the current + previous generation
// survive: "gen.txt" records them, and everything else (including the legacy
// flat "sections/N.bin" layout and root-level "img_*" files) is deleted when
// the current generation changes.

uint32_t fnvFold(uint32_t h, const void* data, size_t len) {
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; i++) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
}

template <typename T>
uint32_t fnvFoldPod(uint32_t h, const T& v) {
  return fnvFold(h, &v, sizeof(v));
}

// Double the page LUT with the nothrow allocator (a std::vector push_back would
// abort() on OOM under -fno-exceptions). Capped at UINT16_MAX entries to match
// the on-disk uint16_t page count. Returns false when the LUT cannot grow.
template <typename Entry>
bool ensurePageLutCapacity(std::unique_ptr<Entry[]>& lut, uint16_t& lutCapacity, const uint16_t lutCount) {
  if (lutCount < lutCapacity) return true;
  if (lutCapacity == UINT16_MAX) return false;

  uint32_t nextCapacity = static_cast<uint32_t>(lutCapacity) * 2U;
  if (nextCapacity > UINT16_MAX) {
    nextCapacity = UINT16_MAX;
  }

  auto grown = makeUniqueNoThrow<Entry[]>(nextCapacity);
  if (!grown) return false;

  for (uint16_t i = 0; i < lutCount; i++) {
    grown[i] = lut[i];
  }
  lut = std::move(grown);
  lutCapacity = static_cast<uint16_t>(nextCapacity);
  return true;
}

// Generation dir already prepared this session; avoids re-scanning the
// sections root on every section open.
std::string s_preparedGenDir;

// Generation cleanup runs from the reader's warm/prefetch pumps, i.e. on a
// heap that can be starved and fragmented. Collecting names into a growing
// std::vector<std::string> used the THROWING allocator: a bad_alloc during
// vector growth abort()ed on device (crash report 2026-07-16, warming a cold
// chapter). Both sweeps below collect at most kSweepBatch fixed-size names
// per pass into ONE nothrow scratch buffer, close the directory handle,
// delete the batch, and rescan; paths are built in fixed char buffers so no
// allocation happens after the scratch check.
constexpr size_t kSweepBatch = 16;
constexpr size_t kSweepNameCap = 128;
constexpr int kSweepMaxPasses = 64;  // backstop: an undeletable entry must not spin forever

void cleanLegacyRootImages(const std::string& cacheDir) {
  auto scratch = makeUniqueNoThrow<char[]>(kSweepBatch * kSweepNameCap);
  if (!scratch) {
    LOG_ERR("SCT", "OOM: legacy image sweep scratch, deferring cleanup");
    return;
  }
  for (int pass = 0; pass < kSweepMaxPasses; ++pass) {
    size_t count = 0;
    {
      auto root = Storage.open(cacheDir.c_str());
      if (!root || !root.isDirectory()) return;
      char name[128];
      for (auto f = root.openNextFile(); f && count < kSweepBatch; f = root.openNextFile()) {
        f.getName(name, sizeof(name));
        if (!f.isDirectory() && strncmp(name, "img_", 4) == 0) {
          snprintf(scratch.get() + count * kSweepNameCap, kSweepNameCap, "%s", name);
          ++count;
        }
      }
    }  // directory handle closed before any remove
    if (count == 0) return;
    char path[224];
    for (size_t i = 0; i < count; ++i) {
      snprintf(path, sizeof(path), "%s/%s", cacheDir.c_str(), scratch.get() + i * kSweepNameCap);
      Storage.remove(path);
    }
    if (count < kSweepBatch) return;  // that scan reached the end of the dir
  }
}

void prepareGeneration(const std::string& cacheDir, const std::string& genName) {
  const std::string sectionsRoot = cacheDir + "/sections";
  const std::string genDir = sectionsRoot + "/" + genName;
  if (s_preparedGenDir == genDir) return;

  Storage.mkdir(cacheDir.c_str());
  Storage.mkdir(sectionsRoot.c_str());

  const std::string markerPath = sectionsRoot + "/gen.txt";
  std::string current;
  std::string previous;
  {
    HalFile marker;
    if (Storage.openFileForRead("SCT", markerPath, marker)) {
      char buf[24] = {};
      const int n = marker.read(buf, sizeof(buf) - 1);
      if (n > 0) {
        const std::string text(buf);
        const size_t nl = text.find('\n');
        current = text.substr(0, nl);
        if (nl != std::string::npos) previous = text.substr(nl + 1);
        const size_t tail = previous.find('\n');
        if (tail != std::string::npos) previous.resize(tail);
      }
    }
  }

  if (current != genName) {
    // The outgoing current becomes the kept previous; everything else goes,
    // including loose files from the pre-generation flat layout.
    const std::string keepPrev = current;
    // Same batched nothrow sweep as cleanLegacyRootImages (see comment there):
    // entry names + an is-dir flag byte per slot, fixed scratch, no vector.
    auto scratch = makeUniqueNoThrow<char[]>(kSweepBatch * (kSweepNameCap + 1));
    if (scratch) {
      for (int pass = 0; pass < kSweepMaxPasses; ++pass) {
        size_t count = 0;
        {
          auto root = Storage.open(sectionsRoot.c_str());
          if (!root || !root.isDirectory()) break;
          char name[128];
          for (auto f = root.openNextFile(); f && count < kSweepBatch; f = root.openNextFile()) {
            f.getName(name, sizeof(name));
            const bool isDir = f.isDirectory();
            const bool keep = isDir ? (genName == name || keepPrev == name) : (strcmp(name, "gen.txt") == 0);
            if (keep) continue;
            char* slot = scratch.get() + count * (kSweepNameCap + 1);
            slot[0] = isDir ? 1 : 0;
            snprintf(slot + 1, kSweepNameCap, "%s", name);
            ++count;
          }
        }  // directory handle closed before any remove
        if (count == 0) break;
        char path[224];
        for (size_t i = 0; i < count; ++i) {
          const char* slot = scratch.get() + i * (kSweepNameCap + 1);
          snprintf(path, sizeof(path), "%s/%s", sectionsRoot.c_str(), slot + 1);
          if (slot[0]) {
            Storage.removeDir(path);
          } else {
            Storage.remove(path);
          }
        }
        if (count < kSweepBatch) break;
      }
    } else {
      LOG_ERR("SCT", "OOM: generation sweep scratch, deferring cleanup");
    }
    cleanLegacyRootImages(cacheDir);

    HalFile marker;
    // Explicit rewrite of a small marker: openFileForWrite truncates.
    if (Storage.openFileForWrite("SCT", markerPath, marker)) {
      const std::string text = genName + "\n" + keepPrev + "\n";
      marker.write(text.c_str(), text.size());
    }
    LOG_INF("SCT", "Section cache generation switched to %s (kept previous: %s)", genName.c_str(),
            keepPrev.empty() ? "none" : keepPrev.c_str());
  }

  Storage.mkdir(genDir.c_str());
  s_preparedGenDir = genDir;
}

}  // namespace

Section::Section(const std::shared_ptr<Epub>& epub, const int spineIndex, GfxRenderer& renderer)
    : epub(epub),
      spineIndex(spineIndex),
      renderer(renderer),
      filePath(epub->getCachePath() + "/sections/" + std::to_string(spineIndex) + ".bin") {}

// Out-of-line so BuildContext (which holds a unique_ptr to the forward-declared
// ChapterHtmlSlimParser) is a complete type here. Any in-flight build is torn down.
Section::~Section() { abandonBuild(); }

namespace {
// The section-cache generation id: the layout identity (LayoutParams::hash, which
// folds every layout-affecting field) combined with the CSS-engine version, so a
// CSS change lands the section in a fresh generation directory. This is the single
// place that decides "which cache drawer does this layout live in".
uint32_t generationHash(const LayoutParams& lp) {
  uint32_t h = lp.hash();
  h = fnvFoldPod(h, CssParser::CSS_CACHE_VERSION);
  return h;
}
}  // namespace

void Section::selectGeneration(const LayoutParams& lp) {
  const uint32_t h = generationHash(lp);

  char genName[12];
  snprintf(genName, sizeof(genName), "%08x", static_cast<unsigned>(h));

  const std::string& cacheDir = epub->getCachePath();
  prepareGeneration(cacheDir, genName);
  sectionDirPath = cacheDir + "/sections/" + genName;
  filePath = sectionDirPath + "/" + std::to_string(spineIndex) + ".bin";
}

bool Section::hasCachedSectionFor(const LayoutParams& lp) const {
  // Pure existence probe: no prepareGeneration, no marker switch, no prune,
  // no member mutation. Callers scanning several parameter candidates (the
  // low-memory tier adoption in loadSectionFromCache) MUST use this first —
  // loadSectionFile's generation switch prunes all but the newest two
  // generation dirs, so probing by loading would delete the very drawer a
  // later candidate needs.
  const uint32_t h = generationHash(lp);
  char genName[12];
  snprintf(genName, sizeof(genName), "%08x", static_cast<unsigned>(h));
  const std::string candidate =
      epub->getCachePath() + "/sections/" + genName + "/" + std::to_string(spineIndex) + ".bin";
  return Storage.exists(candidate.c_str());
}

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

bool Section::writeSectionFileHeader(const LayoutParams& lp) {
  if (!file) {
    LOG_DBG("SCT", "File not open for writing header");
    return false;
  }
  static_assert(HEADER_SIZE == sizeof(SECTION_FILE_VERSION) + sizeof(lp.fontId) + sizeof(lp.lineCompression) +
                                   sizeof(lp.extraParagraphSpacing) + sizeof(lp.paragraphAlignment) +
                                   sizeof(lp.viewportWidth) + sizeof(lp.viewportHeight) + sizeof(pageCount) +
                                   sizeof(lp.hyphenationEnabled) + sizeof(lp.embeddedStyle) +
                                   sizeof(lp.imageRendering) + sizeof(lp.focusReadingEnabled) +
                                   sizeof(lp.guideDotsEnabled) + sizeof(lp.firstLineIndentPx) + sizeof(lp.wordSpacing) +
                                   sizeof(lp.paragraphSpacing) + sizeof(uint32_t) + sizeof(uint32_t) +
                                   sizeof(uint32_t) + sizeof(uint32_t),
                "Header size mismatch");
  // Version byte starts as INCOMPLETE; createSectionFile stamps the real version
  // last, once the file is fully written, as the atomic commit point.
  // Field order below is LayoutParams' declaration order and MUST match the read
  // order in loadSectionFile (a mismatch fails the compare and rebuilds — safe,
  // never a stale hit).
  const bool ok =
      serialization::tryWritePod(file, SECTION_FILE_INCOMPLETE_VERSION) &&
      serialization::tryWritePod(file, lp.fontId) && serialization::tryWritePod(file, lp.lineCompression) &&
      serialization::tryWritePod(file, lp.extraParagraphSpacing) &&
      serialization::tryWritePod(file, lp.paragraphAlignment) && serialization::tryWritePod(file, lp.viewportWidth) &&
      serialization::tryWritePod(file, lp.viewportHeight) && serialization::tryWritePod(file, lp.hyphenationEnabled) &&
      serialization::tryWritePod(file, lp.embeddedStyle) && serialization::tryWritePod(file, lp.imageRendering) &&
      serialization::tryWritePod(file, lp.focusReadingEnabled) &&
      serialization::tryWritePod(file, lp.guideDotsEnabled) && serialization::tryWritePod(file, lp.firstLineIndentPx) &&
      serialization::tryWritePod(file, lp.wordSpacing) && serialization::tryWritePod(file, lp.paragraphSpacing) &&
      // Placeholders for page count and the four trailer offsets (patched in finalizeBuild)
      serialization::tryWritePod(file, pageCount) && serialization::tryWritePod(file, static_cast<uint32_t>(0)) &&
      serialization::tryWritePod(file, static_cast<uint32_t>(0)) &&
      serialization::tryWritePod(file, static_cast<uint32_t>(0)) &&
      serialization::tryWritePod(file, static_cast<uint32_t>(0));
  if (!ok) {
    LOG_ERR("SCT", "Failed to write section header (SD full or IO error)");
  }
  return ok;
}

bool Section::loadSectionFile(const LayoutParams& lp) {
  selectGeneration(lp);
  if (!Storage.openFileForRead("SCT", filePath, file)) {
    return false;
  }

  // Match parameters
  {
    uint8_t version = 0;
    if (!serialization::tryReadPod(file, version) || version != SECTION_FILE_VERSION) {
      // Explicit close() required: member variable persists beyond function scope
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Unknown version %u", version);
      clearCache();
      return false;
    }

    // Read the stored layout identity, in the same field order writeSectionFileHeader
    // wrote it, then compare fieldwise via the defaulted operator==. A dropped or
    // reordered field can only cause a rebuild here, never a stale-layout cache hit.
    LayoutParams fromFile;
    serialization::readPod(file, fromFile.fontId);
    serialization::readPod(file, fromFile.lineCompression);
    serialization::readPod(file, fromFile.extraParagraphSpacing);
    serialization::readPod(file, fromFile.paragraphAlignment);
    serialization::readPod(file, fromFile.viewportWidth);
    serialization::readPod(file, fromFile.viewportHeight);
    serialization::readPod(file, fromFile.hyphenationEnabled);
    serialization::readPod(file, fromFile.embeddedStyle);
    serialization::readPod(file, fromFile.imageRendering);
    serialization::readPod(file, fromFile.focusReadingEnabled);
    serialization::readPod(file, fromFile.guideDotsEnabled);
    serialization::readPod(file, fromFile.firstLineIndentPx);
    serialization::readPod(file, fromFile.wordSpacing);
    serialization::readPod(file, fromFile.paragraphSpacing);

    if (!(fromFile == lp)) {
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Parameters do not match");
      clearCache();
      return false;
    }
  }

  if (!serialization::tryReadPod(file, pageCount)) {
    file.close();
    LOG_ERR("SCT", "Deserialization failed: truncated page count");
    clearCache();
    return false;
  }
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

bool Section::createSectionFile(const LayoutParams& lp, const std::function<void()>& popupFn) {
  // One-shot: prime the build, then lay out every page and commit in a single call.
  if (!startBuild(lp, popupFn)) {
    return false;
  }
  return buildSomeMore(0);
}

bool Section::startBuild(const LayoutParams& lp, const std::function<void()>& popupFn) {
  // Abandon any prior in-progress build before starting a new one.
  abandonBuild();
  lastBuildLowMemory_ = false;
  // Clear cancel state so a cancel aimed at a previous build never carries into
  // this one; this build owns the flag from here until it ends.
  buildCancelRequested_ = false;
  lastBuildCancelled_ = false;

  selectGeneration(lp);

  const auto localPath = epub->getSpineItem(spineIndex).href;
  const auto tmpHtmlPath = epub->getCachePath() + "/.tmp_" + std::to_string(spineIndex) + ".html";

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
  if (!writeSectionFileHeader(lp)) {
    // Explicit close() required before Storage.remove() on the same path
    file.close();
    Storage.remove(partPath.c_str());
    Storage.remove(tmpHtmlPath.c_str());
    return false;
  }
  pageCount = 0;
  builtPageCount_ = 0;
  buildComplete_ = false;

  // Derive the content base directory and image cache path prefix for the parser
  const size_t lastSlash = localPath.find_last_of('/');
  const std::string contentBase = (lastSlash != std::string::npos) ? localPath.substr(0, lastSlash + 1) : "";
  // Image files live inside the generation dir: serialized pages reference
  // them by absolute path, so each generation stays self-contained and dies
  // with its directory.
  const std::string imageBasePath = sectionDirPath + "/img_" + std::to_string(spineIndex) + "_";

  CssParser* cssParser = nullptr;
  if (lp.embeddedStyle) {
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

  build_->lut = makeUniqueNoThrow<PageLutEntry[]>(INITIAL_SECTION_PAGE_LUT_ENTRIES);
  if (!build_->lut) {
    LOG_ERR("SCT", "OOM: page LUT (%u entries)", INITIAL_SECTION_PAGE_LUT_ENTRIES);
    abandonBuild();
    return false;
  }
  build_->lutCapacity = INITIAL_SECTION_PAGE_LUT_ENTRIES;

  build_->parser = makeUniqueNoThrow<ChapterHtmlSlimParser>(
      epub, build_->tmpHtmlPath, renderer, lp,
      [this](std::unique_ptr<Page> page, const uint16_t paragraphIndex, const uint16_t listItemIndex) {
        if (!build_ || build_->lutFailed) {
          return;
        }
        if (!ensurePageLutCapacity(build_->lut, build_->lutCapacity, build_->lutCount)) {
          LOG_ERR("SCT", "Failed to grow section page LUT from %u entries", build_->lutCapacity);
          // At the uint16 format cap this is a hard error; otherwise it is an
          // OOM, so flag it low-memory and let the caller degrade tier + retry.
          lastBuildLowMemory_ = build_->lutCapacity != UINT16_MAX;
          build_->lutFailed = true;
          return;
        }
        build_->lut[build_->lutCount++] = {onPageComplete(std::move(page)), paragraphIndex, listItemIndex};
        builtPageCount_ = build_->lutCount;
      },
      contentBase, imageBasePath, std::move(tocAnchors), popupFn, cssParser);
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
    // Cooperative cancel: a foreground caller (the reader) can abandon a heavy
    // build so it reclaims the render lock promptly instead of waiting out the
    // whole layout. Checked first so cancel always wins over the page budget.
    if (buildCancelRequested_) {
      LOG_INF("SCT", "Build cancelled by request after %u pages", (unsigned)builtPageCount_);
      lastBuildCancelled_ = true;
      abandonBuild();
      return false;
    }
    // Yield once this call has laid out its budget of pages; the build stays live.
    if (maxPages > 0 && static_cast<int>(builtPageCount_ - startCount) >= maxPages) {
      return true;
    }
    const auto status = build_->parser->parseStep();
    // The page callback marks lutFailed when the LUT cannot grow; the failure
    // (and its lastBuildLowMemory_ classification) takes precedence over the
    // parse status, so check it before anything else.
    if (build_->lutFailed) {
      LOG_ERR("SCT", "Abandoning build: page LUT exhausted");
      abandonBuild();
      return false;
    }
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
    if (build_->lutFailed) {
      LOG_ERR("SCT", "Abandoning build: page LUT exhausted on final page");
      abandonBuild();
      return false;
    }
    return finalizeBuild();
  }
}

bool Section::finalizeBuild() {
  if (!build_) {
    return false;
  }
  // The unzipped HTML is no longer needed once layout is complete.
  Storage.remove(build_->tmpHtmlPath.c_str());

  // Every trailer write is checked: a short write here (SD full / IO error) must
  // abandon the .part instead of stamping a truncated file as a valid cache.
  bool writeOk = true;

  // Write the page LUT (byte offset of each serialized page).
  const uint32_t lutOffset = file.position();
  for (uint16_t i = 0; i < build_->lutCount; i++) {
    if (build_->lut[i].fileOffset == 0) {
      LOG_ERR("SCT", "Failed to write LUT due to invalid page positions");
      abandonBuild();
      return false;
    }
    writeOk = writeOk && serialization::tryWritePod(file, build_->lut[i].fileOffset);
  }

  // Write anchor-to-page map for fragment navigation (e.g. footnote targets).
  const uint32_t anchorMapOffset = file.position();
  const auto& anchors = build_->parser->getAnchors();
  writeOk = writeOk && serialization::tryWritePod(file, static_cast<uint16_t>(anchors.size()));
  for (const auto& [anchor, page] : anchors) {
    writeOk = writeOk && serialization::tryWriteString(file, anchor) && serialization::tryWritePod(file, page);
  }

  // Paragraph LUT (synthetic XPath p[N] -> page).
  const uint32_t paragraphLutOffset = file.position();
  writeOk = writeOk && serialization::tryWritePod(file, build_->lutCount);
  for (uint16_t i = 0; i < build_->lutCount; i++) {
    writeOk = writeOk && serialization::tryWritePod(file, build_->lut[i].paragraphIndex);
  }

  // Li LUT (running list-item index -> page); shares its count with the paragraph LUT.
  const uint32_t liLutFileOffset = static_cast<uint32_t>(file.position());
  for (uint16_t i = 0; i < build_->lutCount; i++) {
    writeOk = writeOk && serialization::tryWritePod(file, build_->lut[i].listItemIndex);
  }

  // Patch header with final pageCount and the four trailer offsets.
  writeOk = writeOk && file.seek(HEADER_SIZE - sizeof(uint32_t) * 4 - sizeof(pageCount)) &&
            serialization::tryWritePod(file, pageCount) && serialization::tryWritePod(file, lutOffset) &&
            serialization::tryWritePod(file, anchorMapOffset) && serialization::tryWritePod(file, paragraphLutOffset) &&
            serialization::tryWritePod(file, liLutFileOffset);

  if (!writeOk) {
    LOG_ERR("SCT", "Failed to write section trailer (SD full or IO error)");
    abandonBuild();
    return false;
  }

  // Commit point: stamp the real version byte LAST, after every offset is patched.
  // A crash before this leaves the .part at INCOMPLETE (0), which loadSectionFile
  // rejects, so a partially written build is never mistaken for a finished cache.
  if (!file.seek(0) || !serialization::tryWritePod(file, SECTION_FILE_VERSION)) {
    LOG_ERR("SCT", "Failed to stamp section version (SD full or IO error)");
    abandonBuild();
    return false;
  }
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
  if (build_ && page >= 0 && static_cast<uint16_t>(page) < build_->lutCount) {
    // Page already laid out by the active build: read it straight from the .part
    // using the in-RAM offset (the on-disk LUT/trailers are not committed yet).
    if (!Storage.openFileForRead("SCT", build_->partPath, file)) {
      return nullptr;
    }
    if (!file.seek(build_->lut[static_cast<size_t>(page)].fileOffset)) {
      LOG_ERR("SCT", "Page load failed: seek in .part");
      file.close();
      return nullptr;
    }
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

  auto closeAndReturnNull = [this]() -> std::unique_ptr<Page> {
    file.close();
    return nullptr;
  };

  if (!file.seek(HEADER_SIZE - sizeof(uint32_t) * 4)) {
    LOG_ERR("SCT", "Page load failed: header seek");
    return closeAndReturnNull();
  }
  uint32_t lutOffset;
  if (!serialization::tryReadPod(file, lutOffset) || !file.seek(lutOffset + sizeof(uint32_t) * currentPage)) {
    LOG_ERR("SCT", "Page load failed: LUT offset");
    return closeAndReturnNull();
  }
  uint32_t pagePos;
  if (!serialization::tryReadPod(file, pagePos) || !file.seek(pagePos)) {
    LOG_ERR("SCT", "Page load failed: page position");
    return closeAndReturnNull();
  }

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

  // Trust the count only after the same version gate loadSectionFile applies;
  // a stale-format file stores a different layout at the count's offset.
  uint8_t version = 0;
  if (!serialization::tryReadPod(f, version) || version != SECTION_FILE_VERSION) {
    return std::nullopt;
  }

  if (!f.seek(HEADER_SIZE - sizeof(uint32_t) * 4 - sizeof(uint16_t))) {
    return std::nullopt;
  }
  uint16_t count;
  if (!serialization::tryReadPod(f, count)) {
    return std::nullopt;
  }
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
