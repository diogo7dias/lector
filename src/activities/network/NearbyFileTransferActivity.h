#pragma once

#include <HalStorage.h>
#include <NearbyFileGroup.h>
#include <NearbyFilePayloads.h>
#include <NearbyFileRules.h>
#include <NearbyFileSession.h>
#include <NearbyTransfer.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct Rect;

/**
 * Sends files between two readers over ESP-NOW, with no WiFi network, server,
 * or computer involved.
 *
 * A book goes over on its own. A font family goes over as a batch: one face
 * after another into the same folder, with a single question asked at the other
 * end, because a family is one thing to the reader even though it is six files
 * on the card.
 *
 * This is the screens and the SD card. The conversation lives in
 * nearby_file::TransferSession, what may land on the card in
 * nearby_file::NearbyFileRules, and the packet envelope plus chunk accounting in
 * the SDK's freeink::nearby library. All three are covered by host tests; only
 * what is here needs two devices to exercise.
 */
class NearbyFileTransferActivity final : public Activity {
 public:
  enum class Mode : uint8_t { Send, Receive };

  /**
   * `sourcePath` is the file to send, and is ignored when receiving.
   *
   * `returnToReaderPath` is set when this was opened from inside a book: the
   * reader released the book to make room for the radio, so leaving has to reopen
   * it rather than drop the user on the home screen.
   */
  explicit NearbyFileTransferActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode,
                                      std::string sourcePath = {}, std::string returnToReaderPath = {});

  /**
   * Sends a whole font family: every face in `facePaths` into ".fonts/<family>"
   * on the other reader, asked about once rather than face by face.
   *
   * `totalBytes` is what the family costs on the card, so the question put at
   * the other end names the whole family rather than its first face. The caller
   * has already read those sizes to list the family, so they are passed in
   * rather than read off the card a second time.
   */
  static std::unique_ptr<NearbyFileTransferActivity> sendFontFamily(GfxRenderer& renderer,
                                                                    MappedInputManager& mappedInput,
                                                                    const std::string& familyName,
                                                                    std::vector<std::string> facePaths,
                                                                    uint64_t totalBytes);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // A transfer stops the moment this screen sleeps, which would strand the other
  // reader waiting on chunks that never come.
  bool preventAutoSleep() override { return true; }
  // Chunks arrive as fast as the radio and the card allow; a loop delay here
  // would throttle the whole transfer.
  bool skipLoopDelay() override;

 private:
  void pumpRadio();
  void runSessionActions();
  bool sendPacket(freeink::nearby::PacketType type, const std::array<uint8_t, 6>& peerMac, uint32_t sequence,
                  const void* payload, uint16_t payloadLength);
  /** Reads one slice of the outgoing file and puts it on the air. */
  void sendChunk(const nearby_file::TransferAction& action);
  /** Writes one arriving chunk to the destination file. */
  bool writeChunk(const uint8_t* data, size_t length);
  void handleOffer(const nearby_file::OfferPayload& offer, const std::array<uint8_t, 6>& sourceMac);
  void acceptIncomingOffer();
  /** Opens the file at `sourceIndex` and sizes it. False when it cannot be read. */
  bool openCurrentSource();
  /** Sender: moves on to the next file of a batch without hunting for the peer again. */
  void advanceToNextSource();
  /** Receiver: gets ready for the next file of a batch already accepted. */
  void awaitNextGroupFile();
  /**
   * Receiver: the folder an accepted font face goes in, created if it is not
   * there. Empty when the family is already installed or the folder cannot be
   * made, with `errorMessage` set to why.
   */
  std::string prepareFontFolder(const std::string& familyName);
  /** Removes a family folder left half-installed by a batch that did not finish. */
  void discardPartialFamily();
  /** Removes a partly written file after a failed or cancelled transfer. */
  void discardPartialFile();
  void closeFiles();
  void finishWithError(const char* message);
  /** Leaves the screen, back to the book when it was opened from one. */
  void leave();

  /** What the file on its way over is called on screen: a family, or a filename. */
  std::string sendLabel() const;

  /** Wrapping area for a line of explanation under a heading. */
  Rect detailBounds(const Rect& screen, int top) const;

  void renderSearching(const Rect& screen, int top) const;
  void renderPeerList(const Rect& screen, int top) const;
  void renderOfferPrompt(const Rect& screen, int top) const;
  void renderProgress(const Rect& screen, int top) const;
  void renderMessage(const Rect& screen, int top, const char* message, const char* detail) const;

  Mode mode;
  // Sending: one book, or every face of one font family, in order.
  std::vector<std::string> sourcePaths;
  size_t sourceIndex = 0;
  /** Destination folder asked of the receiver. Empty for a book. */
  std::string sendFolder;
  uint64_t sendTotalBytes = 0;
  std::string returnToReaderPath;
  std::string sourceName;
  uint64_t sourceSize = 0;

  freeink::nearby::EspNowTransport transport;
  nearby_file::TransferSession session;
  std::array<uint8_t, 6> localMac = {};

  HalFile outgoing;
  HalFile incoming;
  std::string destinationPath;
  bool destinationOpen = false;

  // Receiving: which batch, if any, the files arriving belong to.
  nearby_file::ReceiveGroup group;
  nearby_file::OfferPayload pendingOffer;
  /** Set when this reader created the family folder and may clean it up again. */
  std::string createdFamilyPath;

  // Sending: the reader chosen for the first file, reused for the rest of a
  // batch so each face does not start another round of discovery.
  std::array<uint8_t, 6> chosenPeerMac = {};
  bool hasChosenPeer = false;

  ButtonNavigator buttonNavigator;
  int selectedPeer = 0;
  // Receiver's prompt: 0 accepts, 1 declines.
  int offerChoice = 0;

  bool radioFailed = false;
  bool sourceUnreadable = false;
  std::string errorMessage;

  // Progress is redrawn on a timer rather than per chunk: an e-ink panel cannot
  // keep up with a refresh for every 1024 bytes.
  unsigned long lastProgressDrawMs = 0;
  int lastDrawnPercent = -1;
  static constexpr unsigned long PROGRESS_REDRAW_INTERVAL_MS = 1200;
  /** Lines a wrapped explanation may take before it is cut. */
  static constexpr int DETAIL_MAX_LINES = 3;

  nearby_file::TransferState renderedState = nearby_file::TransferState::LISTENING;
  size_t renderedPeerCount = 0;
  int renderedSelection = -1;
  unsigned long autoReturnAt = 0;
  static constexpr unsigned long AUTO_RETURN_DELAY_MS = 1800;
};
