#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "ReadingStats.h"

namespace reading_stats {

class StatsFiles {
 public:
  virtual ~StatsFiles() = default;
  virtual bool read(const std::string& path, uint8_t* destination, size_t capacity, size_t& size) = 0;
  virtual bool write(const std::string& path, const uint8_t* source, size_t size) = 0;
  virtual bool exists(const std::string& path) const = 0;
  virtual bool remove(const std::string& path) = 0;
  virtual bool rename(const std::string& from, const std::string& to) = 0;
};

enum class StatsLoadResult : uint8_t { Ok, Missing, Invalid, RecoveredBackup, NewerVersion };

class ReadingStatsStore {
 public:
  explicit ReadingStatsStore(StatsFiles& files) : files_(files) {}

  StatsLoadResult load(const std::string& path, ReadingStatsData& stats);
  bool save(const std::string& path, const ReadingStatsData& stats);
  bool reset(const std::string& path);

 private:
  DecodeResult readOne(const std::string& path, ReadingStatsData& stats, bool& found);

  StatsFiles& files_;
  std::string blockedNewerPath_;
};

}  // namespace reading_stats
