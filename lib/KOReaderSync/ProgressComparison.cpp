#include "ProgressComparison.h"

#include <cmath>

namespace {
// Positions within this fraction of each other count as the same position.
// 0.001 is 0.1 percentage points: under half a page in a 400-page book.
constexpr float SAME_PROGRESS_EPSILON = 0.001f;

// A broken or empty server record is not a fraction and cannot be a position, so it
// must never decide the comparison: it lands in Unknown, which asks the reader.
bool isUsablePercentage(const float percentage) {
  return std::isfinite(percentage) && percentage >= 0.0f && percentage <= 1.0f;
}

ProgressComparison compareOrdered(const uint32_t local, const uint32_t remote) {
  if (local > remote) return ProgressComparison::LocalAhead;
  if (local < remote) return ProgressComparison::RemoteAhead;
  return ProgressComparison::Synchronized;
}
}  // namespace

ProgressComparison compareProgress(const CrossPointPosition& local, const float localPercentage,
                                   const CrossPointPosition& remote, const float remotePercentage) {
  if (local.hasResolvedSpineIndex && remote.hasResolvedSpineIndex) {
    if (local.spineIndex != remote.spineIndex) {
      return local.spineIndex > remote.spineIndex ? ProgressComparison::LocalAhead : ProgressComparison::RemoteAhead;
    }

    if (local.hasMappedPage && remote.hasMappedPage && local.pageNumber == remote.pageNumber) {
      return ProgressComparison::Synchronized;
    }

    if (local.hasVisibleTextOffset && remote.hasVisibleTextOffset) {
      return compareOrdered(local.visibleTextOffset, remote.visibleTextOffset);
    }

    if (local.hasMappedPage && remote.hasMappedPage) {
      if (local.pageNumber > remote.pageNumber) return ProgressComparison::LocalAhead;
      if (local.pageNumber < remote.pageNumber) return ProgressComparison::RemoteAhead;
      return ProgressComparison::Synchronized;
    }
  }

  if (isUsablePercentage(localPercentage) && isUsablePercentage(remotePercentage)) {
    const float delta = localPercentage - remotePercentage;
    if (std::fabs(delta) <= SAME_PROGRESS_EPSILON) return ProgressComparison::Synchronized;
    return delta > 0.0f ? ProgressComparison::LocalAhead : ProgressComparison::RemoteAhead;
  }

  return ProgressComparison::Unknown;
}

RemoteRecordChoice selectRemoteRecord(const CrossPointPosition& primary, const float primaryPercentage,
                                      const CrossPointPosition& alternate, const float alternatePercentage) {
  return compareProgress(primary, primaryPercentage, alternate, alternatePercentage) == ProgressComparison::RemoteAhead
             ? RemoteRecordChoice::Alternate
             : RemoteRecordChoice::Primary;
}
