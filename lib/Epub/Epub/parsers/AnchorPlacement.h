#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

// Decides which page each link target lands on while a chapter is laid out. The page open
// when a target is met is the wrong answer when its first line does not fit, or when the
// orphan rule later carries that line to the next page.
class AnchorPlacement {
 public:
  using Anchor = std::pair<uint64_t, uint16_t>;
  static constexpr size_t RECENT_LINES = 6;

  AnchorPlacement() {
    waitingForContent.reserve(8);
    waitingForLine.reserve(8);
  }

  const std::deque<Anchor>& placed() const { return anchors; }
  size_t count() const { return anchors.size() + waitingForContent.size() + waitingForLine.size(); }

  void place(const uint64_t id, const uint16_t page) { anchors.push_back({id, page}); }
  void waitForContent(const uint64_t id) { waitingForContent.push_back(id); }
  void waitForLine(const uint64_t id, const uint32_t offset) { waitingForLine.push_back({id, offset}); }

  void contentPlaced(const uint16_t page) {
    for (const uint64_t id : waitingForContent) anchors.push_back({id, page});
    waitingForContent.clear();
  }

  // A line is about to start at lineStart, so earlier targets sit on the line before it.
  void lineStarting(const uint32_t lineStart, const uint16_t page) { placeOnLastLine(lineStart, false, page); }

  void lineAdded(const uint16_t page) {
    const auto begin = static_cast<uint16_t>(anchors.size());
    contentPlaced(page);
    if (recentCount == RECENT_LINES) {
      std::move(recent.begin() + 1, recent.end(), recent.begin());
      recentCount--;
    }
    recent[recentCount++] = {begin, static_cast<uint16_t>(anchors.size())};
  }

  void blockEnded(const uint16_t page) { placeOnLastLine(0, true, page); }

  void linesCarried(const size_t carry, const uint16_t page) {
    for (size_t i = 0; i < carry && i < recentCount; ++i) {
      const Range& line = recent[recentCount - 1 - i];
      for (size_t a = line.begin; a < line.end; ++a) anchors[a].second = page;
    }
  }

  // Targets of a block that placed nothing land on the last page.
  void finish(const uint16_t page) {
    blockEnded(page);
    contentPlaced(page);
  }

 private:
  struct Range {
    uint16_t begin = 0;
    uint16_t end = 0;
  };

  void placeOnLastLine(const uint32_t before, const bool all, const uint16_t page) {
    const auto begin = static_cast<uint16_t>(anchors.size());
    size_t taken = 0;
    while (taken < waitingForLine.size() && (all || waitingForLine[taken].second < before)) {
      anchors.push_back({waitingForLine[taken].first, page});
      taken++;
    }
    waitingForLine.erase(waitingForLine.begin(), waitingForLine.begin() + static_cast<std::ptrdiff_t>(taken));
    if (taken == 0 || recentCount == 0) return;
    Range& last = recent[recentCount - 1];
    if (last.begin == last.end) last.begin = begin;
    last.end = static_cast<uint16_t>(anchors.size());
  }

  std::deque<Anchor> anchors;
  std::vector<uint64_t> waitingForContent;
  std::vector<std::pair<uint64_t, uint32_t>> waitingForLine;
  std::array<Range, RECENT_LINES> recent{};
  size_t recentCount = 0;
};
