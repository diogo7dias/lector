#include "SdFileIndex.h"

#include <FsHelpers.h>
#include <Logging.h>
#include <esp_random.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <queue>
#include <utility>

namespace {
constexpr size_t NAME_BUFFER_SIZE = 500;
constexpr char INDEX_DIR[] = "/.crosspoint";
constexpr char INDEX_PATH[] = "/.crosspoint/.browse_idx.bin";
// Names sorted in RAM per pass; the merge only holds one head per run. 512 names
// (~20 KB transient) keeps the chunk-sort cheap while bounding peak RAM.
constexpr size_t CHUNK = 512;
}  // namespace

bool SdFileIndex::build(const std::string& dirPath, const AcceptFn& accept) {
  clear();

  Storage.mkdir(INDEX_DIR);
  if (Storage.exists(INDEX_PATH)) {
    Storage.remove(INDEX_PATH);
  }
  namesPath_ = INDEX_PATH;

  auto root = Storage.open(dirPath.c_str());
  if (!root || !root.isDirectory()) {
    LOG_ERR("SDIDX", "Not a directory: %s", dirPath.c_str());
    namesPath_.clear();
    return false;
  }

  HalFile writeFile;
  if (!Storage.openFileForWrite("SDIDX", namesPath_, writeFile)) {
    LOG_ERR("SDIDX", "Could not open index temp for write");
    namesPath_.clear();
    return false;
  }

  auto nameBuf = std::string(NAME_BUFFER_SIZE, '\0');
  size_t dirCount = 0;
  root.rewindDirectory();
  size_t scanned = 0;
  for (auto entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    if ((++scanned & 0xFF) == 0) esp_task_wdt_reset();

    entry.getName(nameBuf.data(), NAME_BUFFER_SIZE);
    const char* name = nameBuf.c_str();
    const bool isDir = entry.isDirectory();
    if (!accept(name, isDir)) {
      continue;
    }

    // Display name carries a trailing '/' for directories (matches the in-RAM path
    // and lets naturalFileLess sort directories first).
    std::string display = name;
    if (isDir) {
      display.push_back('/');
      dirCount++;
    }
    if (display.size() > NAME_BUFFER_SIZE) {
      continue;  // pathological; skip rather than truncate mid-UTF-8
    }

    const auto len = static_cast<uint16_t>(display.size());
    const uint32_t offset = writeFile.position();
    if (writeFile.write(&len, sizeof(len)) != sizeof(len) ||
        writeFile.write(display.data(), display.size()) != display.size()) {
      LOG_ERR("SDIDX", "Index temp write failed");
      writeFile.close();
      clear();
      return false;
    }
    offsets_.push_back(offset);
  }
  root.close();
  // Explicit close() required before reopening the same path for reading.
  writeFile.close();

  if (!Storage.openFileForRead("SDIDX", namesPath_, readFile_)) {
    LOG_ERR("SDIDX", "Could not reopen index temp for read");
    clear();
    return false;
  }

  firstFileIdx_ = dirCount;
  if (!externalSort()) {
    clear();
    return false;
  }

  valid_ = true;
  LOG_DBG("SDIDX", "Built SD index: %u entries (%u dirs)", static_cast<uint32_t>(offsets_.size()),
          static_cast<uint32_t>(dirCount));
  return true;
}

std::string SdFileIndex::readNameAtOffset(uint32_t offset) const {
  if (!readFile_) return {};
  if (!readFile_.seek(offset)) return {};
  uint16_t len = 0;
  if (readFile_.read(&len, sizeof(len)) != static_cast<int>(sizeof(len))) return {};
  if (len == 0 || len > NAME_BUFFER_SIZE) return {};
  std::string name(len, '\0');
  if (readFile_.read(name.data(), len) != static_cast<int>(len)) return {};
  return name;
}

bool SdFileIndex::externalSort() {
  const size_t n = offsets_.size();
  if (n <= 1) return true;

  // Phase 1: natural-sort each CHUNK of offsets using their names loaded into RAM.
  for (size_t c = 0; c < n; c += CHUNK) {
    const size_t e = std::min(c + CHUNK, n);
    std::vector<std::pair<std::string, uint32_t>> buf;
    buf.reserve(e - c);
    for (size_t i = c; i < e; i++) {
      buf.emplace_back(readNameAtOffset(offsets_[i]), offsets_[i]);
    }
    esp_task_wdt_reset();
    std::sort(buf.begin(), buf.end(),
              [](const auto& a, const auto& b) { return FsHelpers::naturalFileLess(a.first, b.first); });
    for (size_t i = c; i < e; i++) {
      offsets_[i] = buf[i - c].second;
    }
  }

  if (n <= CHUNK) return true;  // single run already fully sorted

  // Phase 2: k-way merge of the sorted runs. Each run contributes one head at a
  // time, so RAM stays bounded to the merged output plus one name per run.
  struct Head {
    std::string name;
    uint32_t offset;
    size_t run;
  };
  // Min-heap: priority_queue pops the max, so invert the comparison.
  const auto cmp = [](const Head& a, const Head& b) { return FsHelpers::naturalFileLess(b.name, a.name); };
  std::priority_queue<Head, std::vector<Head>, decltype(cmp)> heap(cmp);

  const size_t numRuns = (n + CHUNK - 1) / CHUNK;
  std::vector<size_t> cursor(numRuns);
  std::vector<size_t> runEnd(numRuns);
  for (size_t r = 0; r < numRuns; r++) {
    cursor[r] = r * CHUNK;
    runEnd[r] = std::min((r + 1) * CHUNK, n);
    heap.push({readNameAtOffset(offsets_[cursor[r]]), offsets_[cursor[r]], r});
    cursor[r]++;
  }

  std::vector<uint32_t> merged;
  merged.reserve(n);
  while (!heap.empty()) {
    if ((merged.size() & 0xFF) == 0) esp_task_wdt_reset();
    const Head top = heap.top();
    heap.pop();
    merged.push_back(top.offset);
    const size_t r = top.run;
    if (cursor[r] < runEnd[r]) {
      heap.push({readNameAtOffset(offsets_[cursor[r]]), offsets_[cursor[r]], r});
      cursor[r]++;
    }
  }
  offsets_ = std::move(merged);
  return true;
}

std::string SdFileIndex::nameAt(size_t i) const {
  if (i >= offsets_.size()) return {};
  return readNameAtOffset(offsets_[i]);
}

void SdFileIndex::shuffleTail() {
  const size_t n = offsets_.size();
  for (size_t i = n; i > firstFileIdx_ + 1; i--) {
    const size_t j = firstFileIdx_ + esp_random() % (i - firstFileIdx_);
    std::swap(offsets_[i - 1], offsets_[j]);
  }
}

size_t SdFileIndex::lowerBound(const std::string& name) const {
  size_t lo = 0;
  size_t hi = offsets_.size();
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    if (FsHelpers::naturalFileLess(readNameAtOffset(offsets_[mid]), name)) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

void SdFileIndex::clear() {
  // Close the read handle before removing the file it points at.
  readFile_.close();
  offsets_.clear();
  offsets_.shrink_to_fit();
  firstFileIdx_ = 0;
  valid_ = false;
  if (!namesPath_.empty() && Storage.exists(namesPath_.c_str())) {
    Storage.remove(namesPath_.c_str());
  }
  namesPath_.clear();
}
