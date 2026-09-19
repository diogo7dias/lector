#pragma once
#include <string>
class Epub {
  std::string cache;

 public:
  explicit Epub(std::string cache) : cache(std::move(cache)) {}
  const std::string& getCachePath() const { return cache; }
};
