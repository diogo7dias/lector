#pragma once

#include "ReadingStatsStore.h"

namespace reading_stats {

class SdStatsFiles final : public StatsFiles {
 public:
  bool read(const std::string& path, uint8_t* destination, size_t capacity, size_t& size) override;
  bool write(const std::string& path, const uint8_t* source, size_t size) override;
  bool exists(const std::string& path) const override;
  bool remove(const std::string& path) override;
  bool rename(const std::string& from, const std::string& to) override;
};

}  // namespace reading_stats
