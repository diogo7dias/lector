#pragma once

#include <HalStorage.h>
#include <NearbyFilePayloads.h>
#include <NearbyFileRules.h>
#include <NearbyFileSession.h>
#include <NearbyTransfer.h>

#include <array>
#include <memory>
#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct Rect;

/**
 * Sends one file between two readers over ESP-NOW, with no WiFi network, server,
 * or computer involved.
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

  /** `sourcePath` is the file to send, and is ignored when receiving. */
  explicit NearbyFileTransferActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode,
                                      std::string sourcePath = {});

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
  /** Removes a partly written file after a failed or cancelled transfer. */
  void discardPartialFile();
  void closeFiles();
  void finishWithError(const char* message);

  void renderSearching(const Rect& screen, int top) const;
  void renderPeerList(const Rect& screen, int top) const;
  void renderOfferPrompt(const Rect& screen, int top) const;
  void renderProgress(const Rect& screen, int top) const;
  void renderMessage(const Rect& screen, int top, const char* message, const char* detail) const;

  Mode mode;
  std::string sourcePath;
  std::string sourceName;
  uint64_t sourceSize = 0;

  freeink::nearby::EspNowTransport transport;
  nearby_file::TransferSession session;
  std::array<uint8_t, 6> localMac = {};

  HalFile outgoing;
  HalFile incoming;
  std::string destinationPath;
  bool destinationOpen = false;

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

  nearby_file::TransferState renderedState = nearby_file::TransferState::LISTENING;
  size_t renderedPeerCount = 0;
  int renderedSelection = -1;
  unsigned long autoReturnAt = 0;
  static constexpr unsigned long AUTO_RETURN_DELAY_MS = 1800;
};
