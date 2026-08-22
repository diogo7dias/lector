#pragma once

#include <NearbyTransfer.h>

#include <array>
#include <cstdint>
#include <string>

#include "NearbyFilePayloads.h"
#include "NearbyFileRules.h"

/**
 * The receiving side of a batch of files that belong together.
 *
 * A book arrives on its own and the reader is asked about it. A font family
 * arrives as one face after another, and asking about each face in turn would be
 * six prompts for one decision, so the first offer is the one that is put to the
 * reader and the rest of the run is taken on the strength of that answer.
 *
 * Taking later files without asking is only safe while the batch is exactly the
 * one that was accepted, so every continuation has to come from the same device,
 * name the same folder, and be the next file in order. Anything else is refused:
 * a second device in radio range must not be able to slip a file into a family
 * this reader agreed to take from someone else.
 *
 * No file access and no radio, so all of it is exercised by host tests.
 */
namespace nearby_file {

/**
 * How long a partly received family waits for its next face before it is
 * abandoned. A sender that walks out of range mid-family must not leave this
 * reader taking files from the next device that offers one.
 */
constexpr uint32_t GROUP_NEXT_FILE_TIMEOUT_MS = 20000;

/** Most files one batch may hold, since its position travels as a single byte. */
constexpr size_t MAX_GROUP_FILES = 255;

enum class GroupDecision : uint8_t {
  PROMPT,       // Ask the reader here: a loose file, or the start of a batch.
  AUTO_ACCEPT,  // The next file of the batch already accepted; no second prompt.
  REJECT,       // Not part of anything this reader agreed to take.
};

class ReceiveGroup {
 public:
  GroupDecision decide(const OfferPayload& offer, const std::array<uint8_t, freeink::nearby::MAC_BYTES>& peerMac,
                       uint32_t nowMs) const;

  /** Records the batch an offer belongs to, once it is going to be written. */
  void onAccepted(const OfferPayload& offer, const std::array<uint8_t, freeink::nearby::MAC_BYTES>& peerMac,
                  uint32_t nowMs);
  /** One file of the batch finished and is on the card. */
  void onFileDone(uint32_t nowMs);
  void reset();

  /** True while files of an accepted batch are still to come. */
  bool expectsMore() const { return filesDone_ < fileCount_; }
  /** True when the rest of the batch has stopped arriving. */
  bool isStale(uint32_t nowMs) const;

  const std::string& folder() const { return folder_; }
  std::string familyName() const { return familyNameFromFolder(folder_); }
  uint8_t fileCount() const { return fileCount_; }
  uint8_t filesDone() const { return filesDone_; }
  uint64_t totalBytes() const { return totalBytes_; }

 private:
  std::array<uint8_t, freeink::nearby::MAC_BYTES> peerMac_ = {};
  std::string folder_;
  uint8_t fileCount_ = 0;
  uint8_t filesDone_ = 0;
  uint64_t totalBytes_ = 0;
  uint32_t lastFileMs_ = 0;
};

}  // namespace nearby_file
