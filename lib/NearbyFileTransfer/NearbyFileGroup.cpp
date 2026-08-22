#include "NearbyFileGroup.h"

namespace nearby_file {

GroupDecision ReceiveGroup::decide(const OfferPayload& offer,
                                   const std::array<uint8_t, freeink::nearby::MAC_BYTES>& peerMac,
                                   const uint32_t nowMs) const {
  if (expectsMore() && !isStale(nowMs)) {
    // A batch is part way through. Only the exact next file of that same batch,
    // from the same device, is taken without asking again.
    const bool sameBatch = peerMac == peerMac_ && offer.folder == folder_ && offer.groupCount == fileCount_ &&
                           offer.groupIndex == filesDone_;
    return sameBatch ? GroupDecision::AUTO_ACCEPT : GroupDecision::REJECT;
  }

  // Nothing in progress: a batch may only be joined at its first file, since a
  // later one means the prompt that would have covered it was never shown.
  if (offer.groupCount > 1 && offer.groupIndex != 0) return GroupDecision::REJECT;
  return GroupDecision::PROMPT;
}

void ReceiveGroup::onAccepted(const OfferPayload& offer, const std::array<uint8_t, freeink::nearby::MAC_BYTES>& peerMac,
                              const uint32_t nowMs) {
  if (offer.groupIndex == 0) {
    peerMac_ = peerMac;
    folder_ = offer.folder;
    fileCount_ = offer.groupCount == 0 ? 1 : offer.groupCount;
    filesDone_ = 0;
    totalBytes_ = offer.groupTotalBytes;
  }
  lastFileMs_ = nowMs;
}

void ReceiveGroup::onFileDone(const uint32_t nowMs) {
  if (filesDone_ < fileCount_) filesDone_++;
  lastFileMs_ = nowMs;
}

void ReceiveGroup::reset() {
  peerMac_ = {};
  folder_.clear();
  fileCount_ = 0;
  filesDone_ = 0;
  totalBytes_ = 0;
  lastFileMs_ = 0;
}

bool ReceiveGroup::isStale(const uint32_t nowMs) const {
  if (!expectsMore()) return false;
  // Wrap-safe: millis() rolls over roughly every 49.7 days.
  return static_cast<uint32_t>(nowMs - lastFileMs_) >= GROUP_NEXT_FILE_TIMEOUT_MS;
}

}  // namespace nearby_file
