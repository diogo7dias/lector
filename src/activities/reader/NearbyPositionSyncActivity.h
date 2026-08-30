#pragma once
#include <Epub.h>

#include <memory>
#include <optional>
#include <string>

#include "NearbyPositionSession.h"
#include "ProgressMapper.h"
#include "activities/UiStatusActivity.h"
#include "network/EspNowLink.h"

/**
 * Nearby Position Sync: trade a reading position with another reader a few
 * metres away, over ESP-NOW, with no WiFi network, server, or account.
 *
 * Unlike KOSync, which asks a server where a book is, this talks straight to the
 * other device. And unlike CrossInk's version, neither side is locked into a
 * role: both readers open this screen, both announce themselves, and both are
 * then shown the same comparison with the same two choices. Pressing the button
 * on the wrong device cannot push an older position over a newer one.
 *
 * The screens and the radio live here. What to send and when lives in
 * nearby_position::SyncSession, and the wire format in NearbyPositionProtocol,
 * both of which are covered by host tests.
 */
class NearbyPositionSyncActivity final : public UiStatusActivity {
 public:
  explicit NearbyPositionSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                      const std::string& epubPath, int currentSpineIndex, int currentPage,
                                      int totalPagesInSpine, SavedProgressPosition localProgress,
                                      std::string localChapterName,
                                      std::optional<uint16_t> currentParagraphIndex = std::nullopt);

  void onEnter() override;
  void onExit() override;
  // The radio is only up while this screen is, so sleeping mid-sync would strand
  // the other reader waiting on a peer that stopped answering.
  bool preventAutoSleep() override { return true; }
  bool isReaderActivity() const override { return true; }

 protected:
  StatusView statusView() const override;
  bool handleCustomInput() override;
  void onBackButton() override;
  void onConfirmButton() override;
  void onChoiceActivated(int index) override;

 private:
  // Which of the two choices the comparison screen offers, in the order the
  // base lists them.
  enum Choice : int { TAKE_THEIRS = 0, SEND_MINE = 1 };

  void pumpRadio();
  void runSessionActions();
  void applyPeerPosition();
  void returnToReader();
  /** Builds the position this device announces, or false when there is none to share. */
  bool prepareLocalPosition();
  /**
   * Loads the EPUB metadata on demand.
   *
   * The reader releases the book before handing over, the same as it does for
   * KOSync, so the radio has the heap it needs. It is only needed again once a
   * peer position actually arrives and has to be mapped onto this device's
   * layout.
   */
  void ensureEpubLoaded();

  /** Builds the comparison screen's own text, once the peer position is mapped. */
  void prepareComparison();
  /** Chapter title for a spine index, falling back to "Section N". */
  std::string chapterNameFor(int spineIndex) const;

  std::shared_ptr<Epub> epub;
  std::string epubPath;
  std::string documentHash;
  std::string localChapterName;
  int currentSpineIndex;
  int currentPage;
  int totalPagesInSpine;
  std::optional<uint16_t> currentParagraphIndex;
  SavedProgressPosition localProgress;

  EspNowLink link;
  nearby_position::SyncSession session;

  // statusView() only hands out pointers, so the comparison's lines have to
  // outlive it.
  std::string peerChapterLine;
  std::string peerPageLine;
  std::string peerDeviceLine;
  std::string localChapterLine;
  std::string localPageLine;
  /** Peer name the lines were built from, so a late NAME packet rebuilds them. */
  std::string renderedPeerName;

  // Set once the peer's position has been mapped onto this device's layout, so
  // the comparison screen can name their chapter and page in local terms.
  CrossPointPosition peerLocalPosition = {};
  bool peerPositionMapped = false;

  bool radioFailed = false;
  bool noLocalPosition = false;
  // Tracks the state the screen was last drawn for, so the activity only
  // repaints an e-ink panel when something actually changed.
  nearby_position::SyncState renderedState = nearby_position::SyncState::SEARCHING;
  bool renderedPeerPosition = false;
  int renderedChoice = TAKE_THEIRS;

  unsigned long autoReturnAt = 0;
  static constexpr unsigned long AUTO_RETURN_DELAY_MS = 1400;
};
