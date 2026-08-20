#pragma once

#include <cstddef>

// Decisions a resumable download makes from the response status alone, kept
// free of Arduino and TLS headers so they can be reasoned about (and tested)
// on their own. A resumed request asks for `bytes=rangeStart-`; what comes
// back decides whether the partial already on disk is worth keeping.
namespace http_range {

// Statuses that carry a body worth writing: 200 is the whole file, 206 the
// requested slice. Everything else is a redirect, an error, or 416.
inline bool isBodyStatus(const int status) { return status == 200 || status == 206; }

// 416 (Range Not Satisfiable) answers a range that starts at or past the end of
// the file, so every byte asked for is already on disk. Only meaningful for a
// request that actually carried a Range header.
inline bool isRangeAlreadyComplete(const int status, const size_t rangeStart) {
  return status == 416 && rangeStart > 0;
}

// What to do with the first body chunk of a (possibly resumed) response.
struct BodyStart {
  // The server sent the whole file in answer to a ranged request, so the bytes
  // already on disk are about to be overwritten rather than appended to.
  bool discardPartial = false;
  // Byte offset the body about to arrive is written at.
  size_t writeOffset = 0;
  // Full size of the finished file, or 0 when the response promised no length
  // (a chunked body), where no size check or progress bar is possible.
  size_t total = 0;
};

inline BodyStart planBodyStart(const int status, const size_t rangeStart, const bool hasLength,
                               const size_t contentLength) {
  BodyStart plan;
  plan.discardPartial = rangeStart > 0 && status != 206;
  plan.writeOffset = plan.discardPartial ? 0 : rangeStart;
  plan.total = hasLength ? plan.writeOffset + contentLength : 0;
  return plan;
}

}  // namespace http_range
