#include "LibraryGroups.h"

#include <algorithm>
#include <cstring>

namespace library {
namespace {

bool equalFold(const std::string_view a, const std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    char ca = a[i];
    char cb = b[i];
    if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
    if (ca != cb) return false;
  }
  return true;
}

}  // namespace

bool buildGroups(LibraryIndexFile& index, const GroupKind kind, std::vector<GroupEntry>& out,
                 const ViewProgressFn progress, void* const progressCtx) {
  out.clear();
  const uint16_t total = index.bookCount();
  if (total == 0) return true;

  struct Item {
    std::string folded;
    std::string display;
    uint16_t count;
  };
  std::vector<Item> items;
  items.reserve(std::min<size_t>(total, 128));

  constexpr size_t BATCH = 4;  // 128 B on stack
  ClixRecord batch[BATCH];
  std::string text;

  for (uint16_t first = 0; first < total; first += BATCH) {
    if (progress && (first & 0x1F) == 0) progress(progressCtx);
    const size_t wanted = std::min<size_t>(BATCH, total - first);
    const size_t got = index.readRecords(first, wanted, batch);
    if (got < wanted) return false;
    for (size_t i = 0; i < got; ++i) {
      const bool ok = kind == GroupKind::Authors ? index.readAuthor(batch[i], text) : index.readSeries(batch[i], text);
      if (!ok || text.empty()) continue;
      const std::string folded = sortFold(text);
      auto it = std::lower_bound(items.begin(), items.end(), folded,
                                 [](const Item& a, const std::string& key) { return a.folded < key; });
      if (it != items.end() && it->folded == folded) {
        if (it->count < UINT16_MAX) ++it->count;
      } else {
        items.insert(it, Item{folded, text, 1});
      }
    }
  }

  out.reserve(items.size());
  for (auto& item : items) {
    out.push_back(GroupEntry{std::move(item.display), item.count});
  }
  return true;
}

bool collectBooksInGroup(LibraryIndexFile& index, const GroupKind kind, const std::string_view groupName,
                         std::vector<uint16_t>& out, const ViewProgressFn progress, void* const progressCtx) {
  out.clear();
  const uint16_t total = index.bookCount();
  if (total == 0 || groupName.empty()) return true;

  struct SeriesRow {
    uint16_t ordinal;
    uint16_t indexX10;
  };
  std::vector<SeriesRow> rows;
  rows.reserve(16);

  constexpr size_t BATCH = 4;
  ClixRecord batch[BATCH];
  std::string text;

  for (uint16_t first = 0; first < total; first += BATCH) {
    if (progress && (first & 0x1F) == 0) progress(progressCtx);
    const size_t wanted = std::min<size_t>(BATCH, total - first);
    const size_t got = index.readRecords(first, wanted, batch);
    if (got < wanted) return false;
    for (size_t i = 0; i < got; ++i) {
      const bool ok = kind == GroupKind::Authors ? index.readAuthor(batch[i], text) : index.readSeries(batch[i], text);
      if (!ok || text.empty() || !equalFold(text, groupName)) continue;
      rows.push_back(SeriesRow{static_cast<uint16_t>(first + i), batch[i].seriesIndexX10});
    }
  }

  if (kind == GroupKind::Series) {
    std::sort(rows.begin(), rows.end(), [](const SeriesRow& a, const SeriesRow& b) {
      const uint32_t ka = a.indexX10 == 0 ? 0x10000U : a.indexX10;
      const uint32_t kb = b.indexX10 == 0 ? 0x10000U : b.indexX10;
      return ka != kb ? ka < kb : a.ordinal < b.ordinal;
    });
  }

  out.reserve(rows.size());
  for (const auto& row : rows) out.push_back(row.ordinal);
  return true;
}

bool findNextInSeries(LibraryIndexFile& index, const std::string_view currentBookPath, std::string& nextPathOut) {
  nextPathOut.clear();
  const uint16_t total = index.bookCount();
  if (total == 0 || currentBookPath.empty()) return false;

  const uint64_t targetHash = clixPathHash(currentBookPath.data(), currentBookPath.size());
  constexpr size_t BATCH = 4;
  ClixRecord batch[BATCH];
  std::string path;
  std::string currentSeries;
  uint16_t currentIndexX10 = 0;
  bool foundCurrent = false;

  for (uint16_t first = 0; first < total && !foundCurrent; first += BATCH) {
    const size_t wanted = std::min<size_t>(BATCH, total - first);
    const size_t got = index.readRecords(first, wanted, batch);
    if (got < wanted) return false;
    for (size_t i = 0; i < got; ++i) {
      if (batch[i].pathHash != targetHash || batch[i].pathLen != currentBookPath.size()) continue;
      if (!index.readPath(batch[i], path) || path != currentBookPath) continue;
      if (batch[i].seriesLen == 0 || batch[i].seriesIndexX10 == 0) return false;
      if (!index.readSeries(batch[i], currentSeries) || currentSeries.empty()) return false;
      currentIndexX10 = batch[i].seriesIndexX10;
      foundCurrent = true;
      break;
    }
  }
  if (!foundCurrent) return false;

  // Prefer exact seriesIndex + 1 (seriesIndexX10 + 10); otherwise take the closest
  // book strictly after currentIndexX10 in the same series.
  const uint32_t idealNext = static_cast<uint32_t>(currentIndexX10) + 10U;
  uint32_t bestIdx = UINT32_MAX;
  ClixRecord bestRec{};
  bool hasBest = false;
  std::string series;

  for (uint16_t first = 0; first < total; first += BATCH) {
    const size_t wanted = std::min<size_t>(BATCH, total - first);
    const size_t got = index.readRecords(first, wanted, batch);
    if (got < wanted) break;
    for (size_t i = 0; i < got; ++i) {
      const ClixRecord& r = batch[i];
      if (r.seriesLen == 0 || r.seriesIndexX10 <= currentIndexX10) continue;
      if (r.pathHash == targetHash && r.pathLen == currentBookPath.size()) continue;
      if (!index.readSeries(r, series) || !equalFold(series, currentSeries)) continue;
      if (static_cast<uint32_t>(r.seriesIndexX10) == idealNext) {
        return index.readPath(r, nextPathOut) && nextPathOut != currentBookPath;
      }
      if (r.seriesIndexX10 < bestIdx) {
        bestIdx = r.seriesIndexX10;
        bestRec = r;
        hasBest = true;
      }
    }
  }
  return hasBest && index.readPath(bestRec, nextPathOut) && nextPathOut != currentBookPath;
}

}  // namespace library
