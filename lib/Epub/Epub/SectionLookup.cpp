#include <BufferedFile.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Serialization.h>

#include <algorithm>

#include "Page.h"
#include "Section.h"
#include "SectionFormat.h"
#include "parsers/ChapterHtmlSlimParser.h"

int Section::getTocIndexForPage(const int page) const {
  const int first = epub.getTocIndexForSpineIndex(spineIndex);
  if (first < 0) return first;
  int selected = first;
  int selectedPage = -1;
  const auto consider = [&](const int index, const int anchorPage) {
    if (anchorPage <= page && (anchorPage > selectedPage || (anchorPage == selectedPage && index > selected))) {
      selected = index;
      selectedPage = anchorPage;
    }
  };

  struct Target {
    uint64_t hash;
    int index;
  };
  // 1 KB of targets per pass over the anchor map, so a spine holding hundreds of stories
  // costs a few map scans instead of one file open per story.
  constexpr size_t BATCH_SIZE = 64;
  auto targets = makeUniqueNoThrow<Target[]>(BATCH_SIZE);
  if (!targets) {
    LOG_ERR("SCT", "OOM: TOC anchor lookup");
    return selected;
  }
  HalFile f;
  uint16_t count = 0;
  std::optional<serialization::BufferedFileReader> reader;
  uint32_t mapStart = 0;
  if (openAnchorMap(f, count)) {
    mapStart = f.position();
    reader.emplace(f, 512);
  }
  size_t pending = 0;
  const auto match = [&](const uint64_t key, const int anchorPage) {
    for (size_t i = 0; i < pending; ++i) {
      if (targets[i].hash == key) consider(targets[i].index, anchorPage);
    }
  };
  const auto resolveBatch = [&]() {
    if (build_ && build_->parser) {
      for (const auto& [key, anchorPage] : build_->parser->getAnchors()) match(key, anchorPage);
    }
    if (reader && reader->seek(mapStart)) {
      for (uint16_t i = 0; i < count; ++i) {
        uint64_t key;
        uint16_t anchorPage;
        if (reader->read(&key, sizeof(key)) != sizeof(key) ||
            reader->read(&anchorPage, sizeof(anchorPage)) != sizeof(anchorPage)) {
          break;
        }
        match(key, anchorPage);
      }
    }
    pending = 0;
  };

  // Same contiguous walk as the build's tocAnchors collection above.
  for (int i = first; i < epub.getTocItemsCount(); ++i) {
    const auto entry = epub.getTocItem(i);
    if (entry.spineIndex != spineIndex) break;
    if (entry.anchor.empty()) {
      consider(i, 0);
      continue;
    }
    targets[pending++] = {arxHash64(entry.anchor), i};
    if (pending == BATCH_SIZE) resolveBatch();
  }
  if (pending > 0) resolveBatch();
  return selected;
}

std::optional<uint16_t> Section::getPageForParagraphIndex(const uint16_t pIndex) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(PARAGRAPH_LUT_FIELD);
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
  f.seek(PARAGRAPH_LUT_FIELD);
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

std::optional<uint16_t> Section::pagesUntilNextParagraph(const uint16_t page) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(PARAGRAPH_LUT_FIELD);
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

  const uint32_t lutEnd = paragraphLutOffset + sizeof(uint16_t) + count * sizeof(uint16_t);
  if (lutEnd > fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset + sizeof(uint16_t) + page * sizeof(uint16_t));
  uint16_t here;
  serialization::readPod(f, here);

  uint16_t spanned = 0;
  for (uint16_t i = page + 1; i < count; i++) {
    uint16_t next;
    serialization::readPod(f, next);
    if (next != here) break;
    spanned++;
  }
  return spanned;
}

std::optional<uint16_t> Section::getPageForListItemIndex(const uint16_t liIndex) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(LI_LUT_FIELD);
  uint32_t liLutOffset;
  serialization::readPod(f, liLutOffset);
  if (liLutOffset == 0 || liLutOffset >= fileSize) {
    return std::nullopt;
  }

  // The li LUT shares count with the paragraph LUT; read count from paragraphLutOffset
  f.seek(PARAGRAPH_LUT_FIELD);
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

std::optional<uint32_t> Section::getVisibleTextOffsetForPage(const uint16_t page) const {
  if (build_ && page < build_->lut.size()) {
    return build_->lut[page].visibleTextOffset;
  }

  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f) || f.size() < HEADER_SIZE) {
    return std::nullopt;
  }

  uint8_t version;
  serialization::readPod(f, version);
  if (version != SECTION_FILE_VERSION && version != SECTION_FILE_PARTIAL_VERSION) {
    return std::nullopt;
  }

  f.seek(PAGE_COUNT_FIELD);
  uint16_t count;
  serialization::readPod(f, count);
  if (page >= count) {
    return std::nullopt;
  }

  uint32_t visibleLutOffset = 0;
  if (f.seek(VISIBLE_LUT_FIELD)) serialization::readPod(f, visibleLutOffset);
  const uint32_t entryOffset = visibleLutOffset + static_cast<uint32_t>(page) * sizeof(uint32_t);
  if (visibleLutOffset < HEADER_SIZE || entryOffset + sizeof(uint32_t) > f.size()) {
    return std::nullopt;
  }

  f.seek(entryOffset);
  uint32_t result;
  serialization::readPod(f, result);
  return result;
}

std::optional<uint16_t> Section::getPageForVisibleTextOffset(const uint32_t offset,
                                                             const bool preferFirstAtOffset) const {
  const auto findInEntries = [offset, preferFirstAtOffset](const auto& entries) -> std::optional<uint16_t> {
    if (entries.empty()) return std::nullopt;
    uint16_t result = 0;
    for (size_t i = 0; i < entries.size(); i++) {
      const uint32_t pageStart = entries[i].visibleTextOffset;
      if (preferFirstAtOffset && pageStart == offset) {
        return static_cast<uint16_t>(i);
      }
      if (pageStart > offset) break;
      result = static_cast<uint16_t>(i);
    }
    return result;
  };

  if (build_ && !build_->lut.empty()) {
    // Resolve within the active build's known range. Later offsets may still be
    // covered by an on-disk partial that the resumed build has not reached yet.
    if (offset <= build_->lut.back().visibleTextOffset) {
      return findInEntries(build_->lut);
    }
  }

  // A from-scratch build (no partial loaded) has nothing readable at filePath yet: the
  // build writes the tmp .bin, so the open below would fail every call until it commits.
  if (build_ && !partial_) {
    return std::nullopt;
  }

  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f) || f.size() < HEADER_SIZE) {
    return std::nullopt;
  }

  uint8_t version;
  serialization::readPod(f, version);
  if (version != SECTION_FILE_VERSION && version != SECTION_FILE_PARTIAL_VERSION) {
    return std::nullopt;
  }
  const bool partial = version == SECTION_FILE_PARTIAL_VERSION;

  f.seek(PAGE_COUNT_FIELD);
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0) {
    return std::nullopt;
  }

  uint32_t visibleLutOffset = 0;
  if (f.seek(VISIBLE_LUT_FIELD)) serialization::readPod(f, visibleLutOffset);
  if (visibleLutOffset < HEADER_SIZE || visibleLutOffset + static_cast<uint32_t>(count) * sizeof(uint32_t) > f.size()) {
    return std::nullopt;
  }

  if (!f.seek(visibleLutOffset)) {
    LOG_ERR("SCT", "Failed to seek visible-text LUT");
    return std::nullopt;
  }
  uint16_t result = 0;
  uint32_t lastPageStart = 0;
  // Batch the reads: one SD read per 32 pages instead of one per page.
  constexpr uint32_t BATCH_PAGE_COUNT = 32;
  uint32_t starts[BATCH_PAGE_COUNT];  // 128-byte stack buffer; offsets stay in file order for ties.
  for (uint32_t base = 0; base < count; base += BATCH_PAGE_COUNT) {
    const uint32_t batch = std::min<uint32_t>(BATCH_PAGE_COUNT, count - base);
    const size_t bytes = batch * sizeof(uint32_t);
    if (f.read(starts, bytes) != static_cast<int>(bytes)) {
      LOG_ERR("SCT", "Failed to read visible-text LUT batch");
      return std::nullopt;
    }
    for (uint32_t i = 0; i < batch; ++i) {
      lastPageStart = starts[i];
      if (lastPageStart > offset) return result;
      result = static_cast<uint16_t>(base + i);
      if (preferFirstAtOffset && lastPageStart == offset) return result;
    }
  }
  if (partial && offset > lastPageStart) {
    return std::nullopt;
  }
  return result;
}
