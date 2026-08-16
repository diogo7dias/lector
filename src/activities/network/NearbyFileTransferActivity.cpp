#include "NearbyFileTransferActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookFilingNames.h"

using namespace nearby_file;
using freeink::nearby::PacketType;

namespace {

constexpr const char* LOG_TAG = "NBFT";
constexpr uint8_t ESPNOW_CHANNEL = 1;
constexpr std::array<uint8_t, 6> BROADCAST_MAC = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

/** Where a received file lands. The card root, matching the web upload. */
constexpr const char* DESTINATION_FOLDER = bookfiling::ROOT_FOLDER;

}  // namespace

NearbyFileTransferActivity::NearbyFileTransferActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                       const Mode mode, std::string sourcePath)
    : Activity("NearbyFileTransfer", renderer, mappedInput), mode(mode), sourcePath(std::move(sourcePath)) {}

void NearbyFileTransferActivity::onEnter() {
  Activity::onEnter();

  // The web server and this cannot hold the radio at the same time.
  if (WiFi.getMode() != WIFI_OFF) {
    radioFailed = true;
    errorMessage = tr(STR_NEARBY_RADIO_BUSY);
    requestUpdate(true);
    return;
  }

  if (mode == Mode::Send) {
    sourceName = std::string(bookfiling::fileNameOf(sourcePath));
    if (!Storage.openFileForRead(LOG_TAG, sourcePath, outgoing) || !outgoing.isOpen()) {
      sourceUnreadable = true;
      errorMessage = tr(STR_NEARBY_CANNOT_READ_FILE);
      requestUpdate(true);
      return;
    }
    sourceSize = outgoing.fileSize64();
    if (sourceSize == 0 || sourceSize > MAX_TRANSFER_BYTES) {
      closeFiles();
      sourceUnreadable = true;
      errorMessage = tr(STR_NEARBY_CANNOT_READ_FILE);
      requestUpdate(true);
      return;
    }
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);
  if (!transport.begin(ESPNOW_CHANNEL)) {
    closeFiles();
    radioFailed = true;
    errorMessage = tr(STR_NEARBY_RADIO_BUSY);
    requestUpdate(true);
    return;
  }
  transport.localMac(localMac.data());

  if (mode == Mode::Send) {
    session.beginSend(sourceName, sourceSize, millis());
  } else {
    session.beginReceive(millis());
  }
  requestUpdate(true);
}

void NearbyFileTransferActivity::onExit() {
  // Order matters: stop the radio before touching the card, so no chunk can
  // arrive for a file that is already closed.
  transport.end();
  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);

  if (session.shouldDiscardPartialFile()) discardPartialFile();
  closeFiles();
  Activity::onExit();
}

bool NearbyFileTransferActivity::skipLoopDelay() { return session.state() == TransferState::TRANSFERRING; }

void NearbyFileTransferActivity::closeFiles() {
  if (outgoing.isOpen()) outgoing.close();
  if (incoming.isOpen()) incoming.close();
  destinationOpen = false;
}

void NearbyFileTransferActivity::discardPartialFile() {
  if (destinationPath.empty()) return;
  // A half-written book is worse than no book: it would sit in the library and
  // fail to open. The file only survives a transfer that verified.
  if (incoming.isOpen()) incoming.close();
  destinationOpen = false;
  Storage.remove(destinationPath.c_str());
  LOG_DBG(LOG_TAG, "Removed the partly written file %s", destinationPath.c_str());
  destinationPath.clear();
}

bool NearbyFileTransferActivity::sendPacket(const PacketType type, const std::array<uint8_t, 6>& peerMac,
                                            const uint32_t sequence, const void* payload,
                                            const uint16_t payloadLength) {
  std::array<uint8_t, freeink::nearby::MAX_PACKET_BYTES> packet = {};
  size_t length = 0;
  if (!freeink::nearby::encodePacket(packet.data(), packet.size(), type, session.sessionId(), sequence, payload,
                                     payloadLength, length)) {
    LOG_ERR(LOG_TAG, "Could not encode packet type %d", static_cast<int>(type));
    return false;
  }
  return transport.send(peerMac.data(), packet.data(), length);
}

void NearbyFileTransferActivity::sendChunk(const TransferAction& action) {
  if (!outgoing.isOpen()) {
    finishWithError(tr(STR_NEARBY_CANNOT_READ_FILE));
    return;
  }

  std::array<uint8_t, TransferSession::chunkBytes()> buffer = {};
  if (!outgoing.seek64(action.offset)) {
    finishWithError(tr(STR_NEARBY_CANNOT_READ_FILE));
    return;
  }
  const int read = outgoing.read(buffer.data(), action.length);
  if (read != static_cast<int>(action.length)) {
    finishWithError(tr(STR_NEARBY_CANNOT_READ_FILE));
    return;
  }

  if (!sendPacket(PacketType::Data, action.peerMac, action.sequence, buffer.data(), action.length)) return;
  session.onChunkSent(action.length, millis());
}

bool NearbyFileTransferActivity::writeChunk(const uint8_t* data, const size_t length) {
  if (!destinationOpen) return false;
  const size_t written = incoming.write(data, length);
  if (written != length) {
    // A card with no room left lands here rather than in a pre-flight check:
    // SdFat offers no cheap free-space figure, and scanning the volume would
    // stall the screen for seconds on a large card.
    LOG_ERR(LOG_TAG, "Short write: %u of %u bytes", (unsigned)written, (unsigned)length);
    return false;
  }
  return true;
}

void NearbyFileTransferActivity::finishWithError(const char* message) {
  errorMessage = message;
  session.cancel(millis());
  // The cancel packet is left for the next drain rather than sent from here:
  // this is reached from inside runSessionActions() while sending a chunk, and
  // draining again from within that loop would nest the same queue.
  discardPartialFile();
  closeFiles();
  requestUpdate(true);
}

void NearbyFileTransferActivity::handleOffer(const OfferPayload& offer, const std::array<uint8_t, 6>& sourceMac) {
  TransferEvent incomingEvent;
  incomingEvent.kind = TransferEventKind::OFFER;
  incomingEvent.sourceMac = sourceMac;
  incomingEvent.deviceName = offer.deviceName;
  incomingEvent.fileName = offer.fileName;
  incomingEvent.fileSize = offer.fileSize;
  session.onEvent(incomingEvent, millis());
}

void NearbyFileTransferActivity::acceptIncomingOffer() {
  // The offered name is checked before anything is opened: it decides both
  // whether the file is allowed at all and what it may be called on the card.
  const OfferCheck check = checkOffer(session.offeredName(), session.offeredSize(), UINT64_MAX);
  if (!check.accepted) {
    errorMessage =
        check.rejection == RejectReason::TOO_LARGE ? tr(STR_NEARBY_NO_ROOM) : tr(STR_NEARBY_UNSUPPORTED_FILE);
    session.rejectOffer(millis());
    runSessionActions();
    requestUpdate(true);
    return;
  }

  const std::string resolved = resolveDestination(
      DESTINATION_FOLDER, check.safeName, [](const std::string& path) { return Storage.exists(path.c_str()); },
      [](const std::string_view name, const std::string_view folder, const int index) {
        return bookfiling::destinationCandidate(name, folder, index);
      });
  if (resolved.empty() || !Storage.openFileForWrite(LOG_TAG, resolved, incoming) || !incoming.isOpen()) {
    errorMessage = tr(STR_NEARBY_CANNOT_WRITE_FILE);
    session.rejectOffer(millis());
    runSessionActions();
    requestUpdate(true);
    return;
  }

  destinationPath = resolved;
  destinationOpen = true;
  session.acceptOffer(resolved, millis());
  requestUpdate(true);
}

void NearbyFileTransferActivity::pumpRadio() {
  freeink::nearby::EspNowTransport::Event raw;
  while (transport.poll(raw)) {
    freeink::nearby::PacketView view;
    if (!freeink::nearby::decodePacket(raw.data.data(), raw.length, view)) continue;

    std::array<uint8_t, 6> sourceMac = raw.sourceMac;
    const uint32_t now = millis();

    // Data is the hot path and carries no app-level payload of its own: the
    // bytes go straight to the card once the session agrees they are due.
    if (view.type == PacketType::Data) {
      if (session.acceptChunk(view.sequence, view.payload, view.payloadLength, now)) {
        if (!writeChunk(view.payload, view.payloadLength)) finishWithError(tr(STR_NEARBY_CANNOT_WRITE_FILE));
      }
      continue;
    }

    TransferEvent incomingEvent;
    incomingEvent.sourceMac = sourceMac;

    switch (view.type) {
      case PacketType::Discover:
        incomingEvent.kind = TransferEventKind::DISCOVER;
        decodeNamePayload(view.payload, view.payloadLength, incomingEvent.deviceName);
        session.onEvent(incomingEvent, now);
        break;
      case PacketType::Advertise:
        incomingEvent.kind = TransferEventKind::ADVERTISE;
        decodeNamePayload(view.payload, view.payloadLength, incomingEvent.deviceName);
        session.onEvent(incomingEvent, now);
        break;
      case PacketType::Offer: {
        OfferPayload offer;
        if (decodeOfferPayload(view.payload, view.payloadLength, offer)) handleOffer(offer, sourceMac);
        break;
      }
      case PacketType::Accept:
        incomingEvent.kind = TransferEventKind::ACCEPT;
        session.onEvent(incomingEvent, now);
        break;
      case PacketType::Reject:
        incomingEvent.kind = TransferEventKind::REJECT;
        session.onEvent(incomingEvent, now);
        break;
      case PacketType::Ack:
        incomingEvent.kind = TransferEventKind::ACK;
        session.onEvent(incomingEvent, now);
        break;
      case PacketType::Complete: {
        CompletePayload complete;
        if (!decodeCompletePayload(view.payload, view.payloadLength, complete)) break;
        incomingEvent.kind = TransferEventKind::COMPLETE;
        incomingEvent.crc32 = complete.crc32;
        session.onEvent(incomingEvent, now);
        // The file is only kept when the bytes on the card match what the sender
        // hashed, so flush before the verdict decides whether to delete it.
        if (destinationOpen) incoming.flush();
        break;
      }
      case PacketType::Result: {
        bool success = false;
        if (!decodeResultPayload(view.payload, view.payloadLength, success)) break;
        incomingEvent.kind = TransferEventKind::RESULT;
        incomingEvent.success = success;
        session.onEvent(incomingEvent, now);
        break;
      }
      case PacketType::Cancel:
        incomingEvent.kind = TransferEventKind::CANCEL;
        session.onEvent(incomingEvent, now);
        break;
    }
  }
}

void NearbyFileTransferActivity::runSessionActions() {
  const std::string deviceName = SETTINGS.getEffectiveDeviceName();
  std::array<uint8_t, freeink::nearby::MAX_PACKET_BYTES> payload = {};
  size_t payloadLength = 0;

  TransferAction action;
  while (session.nextAction(millis(), action)) {
    switch (action.kind) {
      case TransferActionKind::BROADCAST_DISCOVER:
        if (encodeNamePayload(deviceName, payload.data(), payload.size(), payloadLength)) {
          sendPacket(PacketType::Discover, BROADCAST_MAC, 0, payload.data(), payloadLength);
        }
        break;
      case TransferActionKind::SEND_ADVERTISE:
        if (encodeNamePayload(deviceName, payload.data(), payload.size(), payloadLength)) {
          sendPacket(PacketType::Advertise, action.peerMac, 0, payload.data(), payloadLength);
        }
        break;
      case TransferActionKind::SEND_OFFER: {
        OfferPayload offer;
        offer.deviceName = deviceName;
        offer.fileName = sourceName;
        offer.fileSize = sourceSize;
        if (encodeOfferPayload(offer, payload.data(), payload.size(), payloadLength)) {
          sendPacket(PacketType::Offer, action.peerMac, 0, payload.data(), payloadLength);
        }
        break;
      }
      case TransferActionKind::SEND_ACCEPT:
        sendPacket(PacketType::Accept, action.peerMac, 0, nullptr, 0);
        break;
      case TransferActionKind::SEND_REJECT:
        sendPacket(PacketType::Reject, action.peerMac, 0, nullptr, 0);
        break;
      case TransferActionKind::SEND_DATA:
        sendChunk(action);
        break;
      case TransferActionKind::SEND_ACK:
        sendPacket(PacketType::Ack, action.peerMac, action.sequence, nullptr, 0);
        break;
      case TransferActionKind::SEND_COMPLETE: {
        CompletePayload complete;
        complete.crc32 = session.crc32();
        complete.totalBytes = sourceSize;
        if (encodeCompletePayload(complete, payload.data(), payload.size(), payloadLength)) {
          sendPacket(PacketType::Complete, action.peerMac, 0, payload.data(), payloadLength);
        }
        break;
      }
      case TransferActionKind::SEND_RESULT:
        if (encodeResultPayload(action.success, payload.data(), payload.size(), payloadLength)) {
          sendPacket(PacketType::Result, action.peerMac, 0, payload.data(), payloadLength);
        }
        break;
      case TransferActionKind::SEND_CANCEL:
        sendPacket(PacketType::Cancel, action.peerMac, 0, nullptr, 0);
        break;
    }
  }
}

void NearbyFileTransferActivity::loop() {
  if (radioFailed || sourceUnreadable) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      activityManager.popActivity();
    }
    return;
  }

  pumpRadio();
  runSessionActions();

  const TransferState state = session.state();

  if (state == TransferState::PEERS_FOUND) {
    const int peerCount = static_cast<int>(session.peerCount());
    if (peerCount > 0) {
      if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        selectedPeer = (selectedPeer - 1 + peerCount) % peerCount;
        requestUpdate();
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        selectedPeer = (selectedPeer + 1) % peerCount;
        requestUpdate();
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        session.choosePeer(session.peerAt(selectedPeer).mac, millis());
        requestUpdate(true);
        return;
      }
    }
  } else if (state == TransferState::OFFER_PROMPT) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      offerChoice = offerChoice == 0 ? 1 : 0;
      requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (offerChoice == 0) {
        acceptIncomingOffer();
      } else {
        session.rejectOffer(millis());
        runSessionActions();
        requestUpdate(true);
      }
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    // Back during a live transfer tells the other reader rather than just
    // vanishing, so it stops waiting instead of timing out.
    session.cancel(millis());
    runSessionActions();
    activityManager.popActivity();
    return;
  }

  if (state == TransferState::TRANSFERRING) {
    const int percent = session.progressPercent();
    if (percent != lastDrawnPercent && millis() - lastProgressDrawMs >= PROGRESS_REDRAW_INTERVAL_MS) {
      lastProgressDrawMs = millis();
      requestUpdate();
    }
    return;
  }

  // A finished transfer closes its files immediately rather than at exit, so the
  // last bytes are on the card before the screen says so.
  if (state == TransferState::DONE && destinationOpen) {
    incoming.flush();
    incoming.close();
    destinationOpen = false;
  }
  if (session.shouldDiscardPartialFile() && !destinationPath.empty()) discardPartialFile();

  if ((state == TransferState::DONE || state == TransferState::REJECTED || state == TransferState::CANCELLED ||
       state == TransferState::FAILED) &&
      autoReturnAt == 0) {
    autoReturnAt = millis() + AUTO_RETURN_DELAY_MS;
  }
  if (autoReturnAt != 0 && millis() >= autoReturnAt) {
    activityManager.popActivity();
    return;
  }

  if (state != renderedState || session.peerCount() != renderedPeerCount || selectedPeer != renderedSelection) {
    requestUpdate();
  }
}

void NearbyFileTransferActivity::renderMessage(const Rect& screen, const int top, const char* message,
                                               const char* detail) const {
  UITheme::drawCenteredText(renderer, screen, UI_10_FONT_ID, top, message, true, EpdFontFamily::REGULAR);
  if (detail && detail[0] != '\0') {
    UITheme::drawCenteredText(renderer, screen, UI_10_FONT_ID, top + 40, detail);
  }
}

void NearbyFileTransferActivity::renderSearching(const Rect& screen, const int top) const {
  const char* primary = mode == Mode::Send ? tr(STR_NEARBY_LOOKING_FOR_READERS) : tr(STR_NEARBY_WAITING_TO_RECEIVE);
  UITheme::drawCenteredText(renderer, screen, UI_10_FONT_ID, top, primary, true, EpdFontFamily::REGULAR);
  if (mode == Mode::Send) {
    UITheme::drawCenteredText(renderer, screen, UI_10_FONT_ID, top + 40, sourceName.c_str());
  }
}

void NearbyFileTransferActivity::renderPeerList(const Rect& screen, const int top) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int left = screen.x + metrics.contentSidePadding;
  constexpr int rowHeight = 30;

  renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_NEARBY_CHOOSE_READER), true, EpdFontFamily::REGULAR);

  for (size_t index = 0; index < session.peerCount(); index++) {
    const int rowY = top + 40 + static_cast<int>(index) * rowHeight;
    const bool selected = static_cast<int>(index) == selectedPeer;
    if (selected) renderer.fillRect(screen.x, rowY - 2, screen.width - 1, rowHeight);
    const std::string& name = session.peerAt(index).name;
    renderer.drawText(UI_10_FONT_ID, left, rowY, name.empty() ? "Lector" : name.c_str(), !selected);
  }
}

void NearbyFileTransferActivity::renderOfferPrompt(const Rect& screen, const int top) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int left = screen.x + metrics.contentSidePadding;
  constexpr int rowHeight = 30;

  renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_NEARBY_OFFER_QUESTION), true, EpdFontFamily::REGULAR);

  char buffer[220];
  std::snprintf(buffer, sizeof(buffer), tr(STR_NEARBY_SIZE_FORMAT), session.offeredName().c_str(),
                static_cast<unsigned>((session.offeredSize() + 1023) / 1024));
  renderer.drawText(UI_10_FONT_ID, left, top + 45, buffer);

  if (!session.peerName().empty()) {
    std::snprintf(buffer, sizeof(buffer), tr(STR_NEARBY_FROM_FORMAT), session.peerName().c_str());
    renderer.drawText(UI_10_FONT_ID, left, top + 70, buffer);
  }

  const int optionY = top + 110;
  if (offerChoice == 0) renderer.fillRect(screen.x, optionY - 2, screen.width - 1, rowHeight);
  renderer.drawText(UI_10_FONT_ID, left, optionY, tr(STR_NEARBY_ACCEPT), offerChoice != 0);

  if (offerChoice == 1) renderer.fillRect(screen.x, optionY + rowHeight - 2, screen.width - 1, rowHeight);
  renderer.drawText(UI_10_FONT_ID, left, optionY + rowHeight, tr(STR_NEARBY_DECLINE), offerChoice != 1);
}

void NearbyFileTransferActivity::renderProgress(const Rect& screen, const int top) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int left = screen.x + metrics.contentSidePadding;

  const char* title = session.state() == TransferState::VERIFYING || outgoing.isOpen() ? tr(STR_NEARBY_SENDING_FILE)
                                                                                       : tr(STR_NEARBY_RECEIVING_FILE);
  renderer.drawCenteredText(UI_10_FONT_ID, top, title, true, EpdFontFamily::REGULAR);

  const std::string& name = mode == Mode::Send ? sourceName : session.offeredName();
  renderer.drawText(UI_10_FONT_ID, left, top + 45, name.c_str());

  const int percent = session.progressPercent();
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%d%%", percent);
  renderer.drawText(UI_10_FONT_ID, left, top + 75, buffer);

  // A plain filled bar: an e-ink panel cannot animate, so the bar is the whole
  // feedback and it only redraws on the progress timer.
  const int barY = top + 105;
  const int barWidth = screen.width - metrics.contentSidePadding * 2;
  renderer.drawRect(left, barY, barWidth, 14);
  const int filled = std::max(0, std::min(barWidth - 2, (barWidth - 2) * percent / 100));
  if (filled > 0) renderer.fillRect(left + 1, barY + 1, filled, 12);
}

void NearbyFileTransferActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_NEARBY_TRANSFER));

  const int centred = screen.y + screen.height / 2 - 40;
  const int listTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  const TransferState state = session.state();
  renderedState = state;
  renderedPeerCount = session.peerCount();
  renderedSelection = selectedPeer;

  bool showListHints = false;
  if (radioFailed || sourceUnreadable) {
    renderMessage(screen, centred, errorMessage.c_str(), "");
  } else {
    switch (state) {
      case TransferState::DISCOVERING:
      case TransferState::LISTENING:
        renderSearching(screen, centred);
        break;
      case TransferState::PEERS_FOUND:
        renderPeerList(screen, listTop);
        showListHints = true;
        break;
      case TransferState::OFFER_SENT:
        renderMessage(screen, centred, tr(STR_NEARBY_SENDING_FILE), sourceName.c_str());
        break;
      case TransferState::OFFER_PROMPT:
        renderOfferPrompt(screen, listTop);
        showListHints = true;
        break;
      case TransferState::TRANSFERRING:
      case TransferState::VERIFYING:
        renderProgress(screen, listTop);
        break;
      case TransferState::DONE: {
        char buffer[220];
        if (mode == Mode::Receive && !destinationPath.empty()) {
          std::snprintf(buffer, sizeof(buffer), tr(STR_NEARBY_SAVED_AS_FORMAT),
                        std::string(bookfiling::fileNameOf(destinationPath)).c_str());
        } else {
          buffer[0] = '\0';
        }
        renderMessage(screen, centred, tr(STR_NEARBY_TRANSFER_DONE), buffer);
        break;
      }
      case TransferState::REJECTED:
        renderMessage(screen, centred, errorMessage.empty() ? tr(STR_NEARBY_OFFER_DECLINED) : errorMessage.c_str(), "");
        break;
      case TransferState::CANCELLED:
        renderMessage(screen, centred, tr(STR_NEARBY_TRANSFER_CANCELLED), "");
        break;
      case TransferState::FAILED:
        renderMessage(screen, centred, tr(STR_NEARBY_TRANSFER_FAILED), errorMessage.c_str());
        break;
    }
  }

  const auto labels = showListHints
                          ? mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN))
                          : mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
