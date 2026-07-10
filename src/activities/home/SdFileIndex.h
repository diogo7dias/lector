#pragma once

#include <HalStorage.h>

#include <functional>
#include <string>

class SdFileIndex {
 public:
  using AcceptFn = std::function<bool(const char* name, bool isDirectory)>;

  SdFileIndex() = default;
  ~SdFileIndex() { clear(); }
  SdFileIndex(const SdFileIndex&) = delete;
  SdFileIndex& operator=(const SdFileIndex&) = delete;

  bool build(const std::string& directoryPath, const AcceptFn& accept);
  void clear();

  bool valid() const { return valid_; }
  size_t count() const { return count_; }
  size_t firstFileIndex() const { return firstFileIndex_; }
  std::string nameAt(size_t logicalIndex) const;
  void shuffleTail();
  size_t lowerBound(const std::string& name) const;

 private:
  static constexpr size_t NAME_BYTES = 500;
  struct Record {
    char name[NAME_BYTES];
  };
  static_assert(sizeof(Record) == NAME_BYTES);

  std::string finalPath_;
  mutable HalFile readFile_;
  size_t count_ = 0;
  size_t firstFileIndex_ = 0;
  size_t shuffleMultiplier_ = 1;
  size_t shuffleOffset_ = 0;
  bool shuffled_ = false;
  bool valid_ = false;

  static bool recordValid(const Record& record);
  static bool recordLess(const Record& left, const Record& right);
  static bool readRecord(HalFile& file, size_t index, Record& record);
  static bool writeRecord(HalFile& file, const Record& record);
  static bool mergePass(const char* inputPath, const char* outputPath, size_t count, size_t runWidth);
  size_t physicalIndex(size_t logicalIndex) const;
};
