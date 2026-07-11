#include "SdFileIndex.h"

#include <FsHelpers.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_random.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cstring>
#include <string_view>

#include "LargeFolderIndexPolicy.h"

namespace {
constexpr char INDEX_DIR[] = "/.crosspoint";
constexpr char INDEX_PATH_A[] = "/.crosspoint/.browse_index_a.bin";
constexpr char INDEX_PATH_B[] = "/.crosspoint/.browse_index_b.bin";
constexpr size_t WDT_INTERVAL = 256;
}  // namespace

bool SdFileIndex::recordValid(const Record& record) {
  return record.name[0] != '\0' && std::memchr(record.name, '\0', NAME_BYTES) != nullptr;
}

bool SdFileIndex::recordLess(const Record& left, const Record& right) {
  return FsHelpers::naturalFileLess(std::string_view(left.name), std::string_view(right.name));
}

bool SdFileIndex::readRecord(HalFile& file, const size_t index, Record& record) {
  const size_t offset = index * sizeof(Record);
  return file.seek(offset) && file.read(&record, sizeof(record)) == static_cast<int>(sizeof(record)) &&
         recordValid(record);
}

bool SdFileIndex::writeRecord(HalFile& file, const Record& record) {
  return file.write(&record, sizeof(record)) == sizeof(record);
}

bool SdFileIndex::mergePass(const char* inputPath, const char* outputPath, const size_t count, const size_t runWidth) {
  HalFile leftFile;
  HalFile rightFile;
  HalFile outputFile;
  if (!Storage.openFileForRead("SDIDX", inputPath, leftFile) ||
      !Storage.openFileForRead("SDIDX", inputPath, rightFile)) {
    return false;
  }
  if (Storage.exists(outputPath)) Storage.remove(outputPath);
  if (!Storage.openFileForWrite("SDIDX", outputPath, outputFile)) return false;

  size_t written = 0;
  for (size_t start = 0; start < count; start += runWidth * 2) {
    size_t leftIndex = start;
    const size_t leftEnd = std::min(start + runWidth, count);
    size_t rightIndex = leftEnd;
    const size_t rightEnd = std::min(start + runWidth * 2, count);
    Record left{};
    Record right{};
    bool hasLeft = leftIndex < leftEnd && readRecord(leftFile, leftIndex, left);
    bool hasRight = rightIndex < rightEnd && readRecord(rightFile, rightIndex, right);
    if ((leftIndex < leftEnd && !hasLeft) || (rightIndex < rightEnd && !hasRight)) return false;

    while (hasLeft || hasRight) {
      const bool takeLeft = hasLeft && (!hasRight || !recordLess(right, left));
      if (!writeRecord(outputFile, takeLeft ? left : right)) return false;
      if (takeLeft) {
        ++leftIndex;
        hasLeft = leftIndex < leftEnd;
        if (hasLeft && !readRecord(leftFile, leftIndex, left)) return false;
      } else {
        ++rightIndex;
        hasRight = rightIndex < rightEnd;
        if (hasRight && !readRecord(rightFile, rightIndex, right)) return false;
      }
      if ((++written % WDT_INTERVAL) == 0) esp_task_wdt_reset();
    }
  }
  outputFile.flush();
  return written == count;
}

bool SdFileIndex::build(const std::string& directoryPath, const AcceptFn& accept) {
  clear();
  Storage.mkdir(INDEX_DIR);
  Storage.remove(INDEX_PATH_A);
  Storage.remove(INDEX_PATH_B);

  auto directory = Storage.open(directoryPath.c_str());
  if (!directory || !directory.isDirectory()) return false;

  HalFile output;
  if (!Storage.openFileForWrite("SDIDX", INDEX_PATH_A, output)) return false;
  auto run = makeUniqueNoThrow<Record[]>(large_folder_index::SORT_RUN_RECORDS);
  if (!run) {
    LOG_ERR("SDIDX", "OOM: sort run buffer");
    directory.close();
    output.close();
    clear();
    return false;
  }
  Record* const runData = run.get();
  auto abortBuild = [&]() {
    directory.close();
    output.close();
    run.reset();
    clear();
    return false;
  };

  char nameBuffer[NAME_BYTES];
  size_t runCount = 0;
  size_t directoryCount = 0;
  size_t scanned = 0;
  auto flushRun = [&]() -> bool {
    std::sort(runData, runData + runCount, recordLess);
    for (size_t i = 0; i < runCount; ++i) {
      if (!writeRecord(output, run[i])) return false;
    }
    runCount = 0;
    return true;
  };

  directory.rewindDirectory();
  for (auto entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    entry.getName(nameBuffer, sizeof(nameBuffer));
    const bool isDirectory = entry.isDirectory();
    if (!accept(nameBuffer, isDirectory)) continue;
    if (!large_folder_index::canAddEntry(recordCount_)) {
      LOG_ERR("SDIDX", "Folder exceeds %u entry index limit",
              static_cast<unsigned>(large_folder_index::MAX_INDEX_ENTRIES));
      return abortBuild();
    }

    const size_t length = strnlen(nameBuffer, sizeof(nameBuffer));
    if (length == 0 || length >= sizeof(nameBuffer) || (isDirectory && length + 1 >= sizeof(nameBuffer))) continue;
    Record& record = run[runCount++];
    std::memset(&record, 0, sizeof(record));
    std::memcpy(record.name, nameBuffer, length);
    if (isDirectory) {
      record.name[length] = '/';
      ++directoryCount;
    }
    ++recordCount_;
    if (runCount == large_folder_index::SORT_RUN_RECORDS && !flushRun()) {
      return abortBuild();
    }
    if ((++scanned % WDT_INTERVAL) == 0) esp_task_wdt_reset();
  }
  directory.close();
  if (runCount > 0 && !flushRun()) {
    return abortBuild();
  }
  output.flush();
  output.close();
  run.reset();

  const char* inputPath = INDEX_PATH_A;
  const char* outputPath = INDEX_PATH_B;
  for (size_t width = large_folder_index::SORT_RUN_RECORDS; width < recordCount_; width *= 2) {
    if (!mergePass(inputPath, outputPath, recordCount_, width)) {
      clear();
      return false;
    }
    std::swap(inputPath, outputPath);
  }

  finalPath_ = inputPath;
  firstFileIndex_ = directoryCount;
  if (!visibleEntries_.reset(recordCount_)) {
    clear();
    return false;
  }
  indexFile_ = Storage.open(finalPath_.c_str(), O_RDWR);
  if (!indexFile_) {
    clear();
    return false;
  }
  valid_ = true;
  LOG_DBG("SDIDX", "Built fixed-record SD index: %u entries", static_cast<unsigned>(recordCount_));
  return true;
}

bool SdFileIndex::physicalIndex(const size_t visibleIndex, size_t& physicalIndex) const {
  size_t sourceIndex = 0;
  if (!visibleEntries_.sourceIndexAt(visibleIndex, sourceIndex)) return false;
  physicalIndex = shuffled_ ? large_folder_index::mapShuffledIndex(sourceIndex, recordCount_, firstFileIndex_,
                                                                   shuffleMultiplier_, shuffleOffset_)
                            : sourceIndex;
  return true;
}

std::string SdFileIndex::nameAt(const size_t logicalIndex) const {
  if (!valid_) return {};
  size_t recordIndex = 0;
  if (!physicalIndex(logicalIndex, recordIndex)) return {};
  Record record{};
  if (!readRecord(indexFile_, recordIndex, record)) return {};
  return record.name;
}

bool SdFileIndex::eraseAt(const size_t visibleIndex) {
  if (!valid_) return false;
  return visibleEntries_.eraseAt(visibleIndex);
}

bool SdFileIndex::renameAt(const size_t visibleIndex, const std::string& name) {
  if (!valid_ || name.empty() || name.size() >= NAME_BYTES || name.find('\0') != std::string::npos) return false;

  size_t recordIndex = 0;
  if (!physicalIndex(visibleIndex, recordIndex)) return false;
  Record record{};
  std::memcpy(record.name, name.data(), name.size());
  if (!recordValid(record)) return false;

  if (!indexFile_.seek(recordIndex * sizeof(Record)) || !writeRecord(indexFile_, record)) return false;
  indexFile_.flush();
  return true;
}

void SdFileIndex::shuffleTail() {
  const size_t fileCount = recordCount_ > firstFileIndex_ ? recordCount_ - firstFileIndex_ : 0;
  if (fileCount <= 1) return;
  shuffleMultiplier_ = large_folder_index::coprimeMultiplier(esp_random(), fileCount);
  shuffleOffset_ = esp_random() % fileCount;
  shuffled_ = true;
}

size_t SdFileIndex::lowerBound(const std::string& name) const {
  if (shuffled_) {
    for (size_t i = 0; i < count(); ++i) {
      if (nameAt(i) == name) return i;
    }
    return count();
  }
  size_t low = 0;
  size_t high = count();
  while (low < high) {
    const size_t middle = low + (high - low) / 2;
    if (FsHelpers::naturalFileLess(nameAt(middle), name)) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  return low;
}

void SdFileIndex::clear() {
  if (indexFile_) indexFile_.close();
  Storage.remove(INDEX_PATH_A);
  Storage.remove(INDEX_PATH_B);
  finalPath_.clear();
  visibleEntries_.reset(0);
  recordCount_ = 0;
  firstFileIndex_ = 0;
  shuffleMultiplier_ = 1;
  shuffleOffset_ = 0;
  shuffled_ = false;
  valid_ = false;
}
