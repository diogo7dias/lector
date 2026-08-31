#include "NearbyFileTransferActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "FontInstaller.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "activities/ActivityManager.h"
#include "OpdsServerStore.h"
#include "WifiCredentialStore.h"
#include "util/BookFilingNames.h"
#include "util/CredentialBundle.h"

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
                                                       const Mode mode, std::string sourcePath,
                                                       std::string returnToReaderPath)
    : UiStatusActivity("NearbyFileTransfer", renderer, mappedInput),
      mode(mode),
      returnToReaderPath(std::move(returnToReaderPath)) {
  if (!sourcePath.empty()) sourcePaths.push_back(std::move(sourcePath));
}

std::unique_ptr<NearbyFileTransferActivity> NearbyFileTransferActivity::sendFontFamily(
    GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& familyName,
    std::vector<std::string> facePaths, const uint64_t totalBytes) {
  auto activity = std::make_unique<NearbyFileTransferActivity>(renderer, mappedInput, Mode::Send);
  activity->sourcePaths = std::move(facePaths);
  activity->sendFolder = std::string(nearby_file::FONT_FOLDER_ROOT) + "/" + familyName;
  activity->sendTotalBytes = totalBytes;
  return activity;
}

std::string NearbyFileTransferActivity::sendLabel() const {
  if (sendFolder.empty()) return sourceName;
  const std::string family = nearby_file::familyNameFromFolder(sendFolder);
  return family.empty() ? sourceName : family;
}

void NearbyFileTransferActivity::leave() {
  if (!returnToReaderPath.empty()) {
    activityManager.goToReader(returnToReaderPath);
    return;
  }
  activityManager.popActivity();
}

void NearbyFileTransferActivity::onEnter() {
  UiStatusActivity::onEnter();

  // The web server and this cannot hold the radio at the same time.
  if (WiFi.getMode() != WIFI_OFF) {
    radioFailed = true;
    errorMessage = tr(STR_NEARBY_RADIO_BUSY);
    requestUpdate(true);
    return;
  }

  if (mode == Mode::Send) {
    if (sourcePaths.empty() || !openCurrentSource()) {
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
    // The searching screen names what is on its way, so the line exists before
    // the first paint.
    refreshProgressLines();
  } else {
    session.beginReceive(millis());
  }
  requestUpdate(true);
}

bool NearbyFileTransferActivity::openCurrentSource() {
  if (sourceIndex >= sourcePaths.size()) return false;
  const std::string& path = sourcePaths[sourceIndex];
  sourceName = std::string(bookfiling::fileNameOf(path));
  if (outgoing.isOpen()) outgoing.close();
  if (!Storage.openFileForRead(LOG_TAG, path, outgoing) || !outgoing.isOpen()) return false;
  sourceSize = outgoing.fileSize64();
  if (sourceSize == 0 || sourceSize > MAX_TRANSFER_BYTES) {
    outgoing.close();
    return false;
  }
  return true;
}

void NearbyFileTransferActivity::advanceToNextSource() {
  sourceIndex++;
  if (!openCurrentSource()) {
    // The packet goes out directly rather than through the session: the file
    // just sent finished cleanly, so the session is done and has no cancel left
    // to raise. Without it the other reader would sit on a half-installed family
    // until its own timeout cleared it.
    if (hasChosenPeer) sendPacket(PacketType::Cancel, chosenPeerMac, 0, nullptr, 0);
    closeFiles();
    sourceUnreadable = true;
    errorMessage = tr(STR_NEARBY_CANNOT_READ_FILE);
    requestUpdate(true);
    return;
  }

  // A fresh session per file, sent straight to the reader already chosen: the
  // batch was agreed once, so repeating discovery would only ask again.
  session = TransferSession{};
  session.beginSend(sourceName, sourceSize, millis());
  session.choosePeer(chosenPeerMac, millis());
  lastDrawnPercent = -1;
  requestUpdate(true);
}

void NearbyFileTransferActivity::awaitNextGroupFile() {
  session = TransferSession{};
  session.beginReceive(millis());
  destinationPath.clear();
  lastDrawnPercent = -1;
  requestUpdate();
}

void NearbyFileTransferActivity::onExit() {
  // Order matters: stop the radio before touching the card, so no chunk can
  // arrive for a file that is already closed.
  transport.end();
  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);

  if (session.shouldDiscardPartialFile()) discardPartialFile();
  if (group.expectsMore()) discardPartialFamily();
  closeFiles();
  Activity::onExit();
}

bool NearbyFileTransferActivity::skipLoopDelay() { return session.state() == TransferState::TRANSFERRING; }

void NearbyFileTransferActivity::closeFiles() {
  if (outgoing.isOpen()) outgoing.close();
  if (incoming.isOpen()) incoming.close();
  destinationOpen = false;
}

void NearbyFileTransferActivity::importCredentialBundle() {
  const std::string path = destinationPath;
  destinationPath.clear();

  std::string json;
  {
    HalFile file;
    if (Storage.openFileForRead(LOG_TAG, path, file)) {
      json.reserve(static_cast<size_t>(file.fileSize()));
      char buffer[256];
      while (file.available()) {
        const int read = file.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
        if (read <= 0) break;
        json.append(buffer, static_cast<size_t>(read));
      }
      file.close();
    }
  }
  // Gone either way, and before anything is applied: a bundle that turns out to be
  // malformed still had passwords in it.
  Storage.remove(path.c_str());

  credential_bundle::Bundle bundle;
  if (json.empty() || !credential_bundle::parse(json, bundle)) {
    LOG_ERR(LOG_TAG, "Credential bundle could not be read");
    credentialsMessage = tr(STR_CREDENTIALS_REJECTED);
    return;
  }

  int networks = 0;
  for (const auto& entry : bundle.wifi) {
    if (WIFI_STORE.addCredential(entry.ssid, entry.password)) networks++;
  }
  int servers = 0;
  for (const auto& entry : bundle.opds) {
    OpdsServer server;
    server.name = entry.name.empty() ? entry.url : entry.name;
    server.url = entry.url;
    server.username = entry.username;
    server.password = entry.password;
    if (OPDS_STORE.addServer(server)) servers++;
  }
  if (networks > 0) WIFI_STORE.saveToFile();
  if (servers > 0) OPDS_STORE.saveToFile();

  LOG_DBG(LOG_TAG, "Imported %d WiFi networks and %d OPDS servers", networks, servers);
  char buffer[96];
  std::snprintf(buffer, sizeof(buffer), tr(STR_CREDENTIALS_IMPORTED_FORMAT), networks, servers);
  credentialsMessage = buffer;
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
  session.onChunkSent(buffer.data(), action.length, millis());
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
  // A sender repeats its offer until the accept reaches it, so the same offer
  // arrives again while the file it announced is already being written. The
  // session ignores an offer once it is transferring, but this screen would
  // still act on it and reopen the destination, truncating what has landed so
  // far, so only a session still waiting for an offer takes one.
  const TransferState state = session.state();
  if (state != TransferState::LISTENING && state != TransferState::OFFER_PROMPT) return;

  const GroupDecision decision = group.decide(offer, sourceMac, millis());

  if (decision == GroupDecision::REJECT) {
    // Refused before the session hears about it. Between the faces of a family
    // this screen is listening again, and letting a stranger's offer through
    // only to reject it would end the shared session and take the half-received
    // family with it. The other device is told directly instead, so it stops
    // retrying while the family in progress carries on.
    sendPacket(PacketType::Reject, sourceMac, 0, nullptr, 0);
    return;
  }

  TransferEvent incomingEvent;
  incomingEvent.kind = TransferEventKind::OFFER;
  incomingEvent.sourceMac = sourceMac;
  incomingEvent.deviceName = offer.deviceName;
  incomingEvent.fileName = offer.fileName;
  incomingEvent.fileSize = offer.fileSize;
  session.onEvent(incomingEvent, millis());

  // The session drops an offer from anyone other than the sender it is already paired
  // with, so a third reader in range can offer without disturbing the transfer in
  // progress. Taking that offer here anyway would leave the prompt naming a stranger's
  // file while the accept applies ITS folder and type to the real sender's transfer,
  // which then fails the type check and refuses a book the reader did want.
  if (session.peerMacAddress() != sourceMac) return;

  pendingOffer = offer;
  if (decision == GroupDecision::AUTO_ACCEPT) acceptIncomingOffer();
}

std::string NearbyFileTransferActivity::prepareFontFolder(const std::string& familyName) {
  // An install never writes over a family that is already there. Merging two
  // versions of a font would leave a family made of faces from both, and
  // replacing one silently would throw away what the reader chose to keep, so
  // the answer is to say it is installed and let them delete it first.
  if (SdCardFontRegistry::findFamilyRoot(familyName.c_str()) != nullptr) {
    errorMessage = tr(STR_NEARBY_FONT_ALREADY_INSTALLED);
    return {};
  }

  const char* root = SdCardFontRegistry::defaultWriteRoot();
  if (!Storage.exists(root) && !Storage.mkdir(root)) {
    errorMessage = tr(STR_NEARBY_CANNOT_WRITE_FILE);
    return {};
  }

  const std::string folder = std::string(root) + "/" + familyName;
  if (!Storage.exists(folder.c_str()) && !Storage.mkdir(folder.c_str())) {
    errorMessage = tr(STR_NEARBY_CANNOT_WRITE_FILE);
    return {};
  }

  createdFamilyPath = folder;
  return folder;
}

void NearbyFileTransferActivity::discardPartialFamily() {
  if (createdFamilyPath.empty()) return;
  // Half a family is a font with sizes missing, which the reader would offer and
  // then fail to render at. It goes with the transfer that did not finish.
  if (incoming.isOpen()) incoming.close();
  destinationOpen = false;
  Storage.removeDir(createdFamilyPath.c_str());
  LOG_DBG(LOG_TAG, "Removed the half-installed font family %s", createdFamilyPath.c_str());
  createdFamilyPath.clear();
  destinationPath.clear();
  sdFontSystem.markRegistryDirty();
}

void NearbyFileTransferActivity::acceptIncomingOffer() {
  // The offered name is checked before anything is opened: it decides both
  // whether the file is allowed at all and what it may be called on the card.
  errorMessage.clear();
  const OfferCheck check = checkOffer(session.offeredName(), session.offeredSize(), UINT64_MAX, pendingOffer.folder);
  if (!check.accepted) {
    errorMessage =
        check.rejection == RejectReason::TOO_LARGE ? tr(STR_NEARBY_NO_ROOM) : tr(STR_NEARBY_UNSUPPORTED_FILE);
    session.rejectOffer(millis());
    runSessionActions();
    requestUpdate(true);
    return;
  }

  std::string resolved;
  if (check.safeFolder.empty()) {
    resolved = resolveDestination(
        DESTINATION_FOLDER, check.safeName, [](const std::string& path) { return Storage.exists(path.c_str()); },
        [](const std::string_view name, const std::string_view folder, const int index) {
          return bookfiling::destinationCandidate(name, folder, index);
        });
  } else {
    // A font face goes in beside the rest of its family under its own name: a
    // "Literata_14 (2).cpfont" would be a size the registry cannot read back.
    const std::string familyName = familyNameFromFolder(check.safeFolder);
    const std::string folder = createdFamilyPath.empty() ? prepareFontFolder(familyName) : createdFamilyPath;
    if (!folder.empty()) resolved = folder + "/" + check.safeName;
  }

  if (resolved.empty() || !Storage.openFileForWrite(LOG_TAG, resolved, incoming) || !incoming.isOpen()) {
    if (errorMessage.empty()) errorMessage = tr(STR_NEARBY_CANNOT_WRITE_FILE);
    session.rejectOffer(millis());
    runSessionActions();
    requestUpdate(true);
    return;
  }

  destinationPath = resolved;
  destinationOpen = true;
  group.onAccepted(pendingOffer, session.peerMacAddress(), millis());
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
        incomingEvent.sequence = view.sequence;
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
        offer.folder = sendFolder;
        offer.groupIndex = static_cast<uint8_t>(sourceIndex);
        offer.groupCount = static_cast<uint8_t>(sourcePaths.size());
        offer.groupTotalBytes = sendTotalBytes;
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

bool NearbyFileTransferActivity::refreshPeerLabels() {
  std::vector<std::string> next;
  for (size_t index = 0; index < session.peerCount(); ++index) {
    const std::string& name = session.peerAt(index).name;
    // A reader answers the first broadcast before it has said what it is called,
    // so the row starts as the model name and is replaced when the name lands.
    next.push_back(name.empty() ? "Lector" : name);
  }
  if (next == peerLabels) return false;

  peerLabels = std::move(next);
  peerRows.clear();
  // Two passes: the strings must stop moving before their addresses are taken.
  peerRows.reserve(peerLabels.size());
  for (const std::string& label : peerLabels) peerRows.push_back(label.c_str());
  return true;
}

void NearbyFileTransferActivity::refreshOfferLines() {
  char buffer[220];
  const std::string family = nearby_file::familyNameFromFolder(pendingOffer.folder);
  if (!family.empty()) {
    // A family is one thing to the reader, so it is offered by name and by what
    // the whole set costs rather than face by face.
    std::snprintf(buffer, sizeof(buffer), tr(STR_NEARBY_FONT_OFFER_FORMAT), family.c_str(),
                  static_cast<unsigned>(pendingOffer.groupCount),
                  static_cast<unsigned>((pendingOffer.groupTotalBytes + 1023) / 1024));
  } else {
    std::snprintf(buffer, sizeof(buffer), tr(STR_NEARBY_SIZE_FORMAT), session.offeredName().c_str(),
                  static_cast<unsigned>((session.offeredSize() + 1023) / 1024));
  }
  offerLine = buffer;

  if (!session.peerName().empty()) {
    std::snprintf(buffer, sizeof(buffer), tr(STR_NEARBY_FROM_FORMAT), session.peerName().c_str());
    offerFromLine = buffer;
  } else {
    offerFromLine.clear();
  }
}

void NearbyFileTransferActivity::refreshProgressLines() {
  progressName = mode == Mode::Send ? sendLabel() : session.offeredName();

  const unsigned batchCount = mode == Mode::Send ? sourcePaths.size() : group.fileCount();
  const unsigned batchIndex = mode == Mode::Send ? sourceIndex + 1 : group.filesDone() + 1u;
  if (batchCount > 1) {
    char batch[48];
    std::snprintf(batch, sizeof(batch), tr(STR_NEARBY_FILE_OF_FORMAT), batchIndex, batchCount);
    progressBatchLine = batch;
  } else {
    progressBatchLine.clear();
  }
}

void NearbyFileTransferActivity::refreshDoneLine() {
  const std::string installedFamily =
      mode == Mode::Receive ? nearby_file::familyNameFromFolder(pendingOffer.folder) : std::string();
  char buffer[220];
  if (!installedFamily.empty()) {
    std::snprintf(buffer, sizeof(buffer), tr(STR_NEARBY_FONT_INSTALLED_FORMAT), installedFamily.c_str());
    doneLine = buffer;
  } else if (!credentialsMessage.empty()) {
    doneLine = credentialsMessage;
  } else if (mode == Mode::Receive && !destinationPath.empty()) {
    std::snprintf(buffer, sizeof(buffer), tr(STR_NEARBY_SAVED_AS_FORMAT),
                  std::string(bookfiling::fileNameOf(destinationPath)).c_str());
    doneLine = buffer;
  } else {
    doneLine.clear();
  }
}

UiStatusActivity::StatusView NearbyFileTransferActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_NEARBY_TRANSFER);
  if (radioFailed || sourceUnreadable) {
    view.lines = {errorMessage.c_str(), nullptr, nullptr, nullptr};
    return view;
  }

  switch (session.state()) {
    case TransferState::DISCOVERING:
    case TransferState::LISTENING:
      view.lines = {mode == Mode::Send ? tr(STR_NEARBY_LOOKING_FOR_READERS) : tr(STR_NEARBY_WAITING_TO_RECEIVE),
                    mode == Mode::Send ? progressName.c_str() : nullptr, nullptr, nullptr};
      break;
    case TransferState::PEERS_FOUND:
      // The readers that answered, as the answers themselves: one row each,
      // chosen with the same key or tap as any other list.
      view.lines = {tr(STR_NEARBY_CHOOSE_READER), nullptr, nullptr, nullptr};
      view.choiceList = peerRows.data();
      view.choiceListCount = static_cast<int>(peerRows.size());
      view.confirmHint = tr(STR_SELECT);
      break;
    case TransferState::OFFER_SENT:
      view.lines = {tr(STR_NEARBY_SENDING_FILE), sourceName.c_str(), nullptr, nullptr};
      break;
    case TransferState::OFFER_PROMPT:
      view.lines = {tr(STR_NEARBY_OFFER_QUESTION), offerLine.c_str(),
                    offerFromLine.empty() ? nullptr : offerFromLine.c_str(), nullptr};
      view.choices = {tr(STR_NEARBY_ACCEPT), tr(STR_NEARBY_DECLINE)};
      view.confirmHint = tr(STR_SELECT);
      break;
    case TransferState::TRANSFERRING:
    case TransferState::VERIFYING:
      view.lines = {session.state() == TransferState::VERIFYING || outgoing.isOpen() ? tr(STR_NEARBY_SENDING_FILE)
                                                                                     : tr(STR_NEARBY_RECEIVING_FILE),
                    progressName.c_str(), progressBatchLine.empty() ? nullptr : progressBatchLine.c_str(), nullptr};
      view.showProgress = true;
      view.progressValue = session.progressPercent();
      view.progressMax = 100;
      view.backHint = tr(STR_CANCEL);
      break;
    case TransferState::DONE:
      view.lines = {tr(STR_NEARBY_TRANSFER_DONE), doneLine.empty() ? nullptr : doneLine.c_str(), nullptr, nullptr};
      break;
    case TransferState::REJECTED:
      view.lines = {errorMessage.empty() ? tr(STR_NEARBY_OFFER_DECLINED) : errorMessage.c_str(), nullptr, nullptr,
                    nullptr};
      break;
    case TransferState::CANCELLED:
      view.lines = {tr(STR_NEARBY_TRANSFER_CANCELLED), nullptr, nullptr, nullptr};
      break;
    case TransferState::FAILED:
      view.lines = {tr(STR_NEARBY_TRANSFER_FAILED), errorMessage.empty() ? nullptr : errorMessage.c_str(), nullptr,
                    nullptr};
      break;
  }
  return view;
}

void NearbyFileTransferActivity::onChoiceActivated(const int index) {
  const TransferState state = session.state();
  if (state == TransferState::PEERS_FOUND) {
    if (index < 0 || index >= static_cast<int>(session.peerCount())) return;
    chosenPeerMac = session.peerAt(index).mac;
    hasChosenPeer = true;
    session.choosePeer(chosenPeerMac, millis());
    requestUpdate(true);
    return;
  }
  if (state != TransferState::OFFER_PROMPT) return;
  if (index == 0) {
    acceptIncomingOffer();
    return;
  }
  session.rejectOffer(millis());
  runSessionActions();
  requestUpdate(true);
}

// Back during a live transfer tells the other reader rather than just
// vanishing, so it stops waiting instead of timing out.
void NearbyFileTransferActivity::onBackButton() {
  if (!radioFailed && !sourceUnreadable) {
    session.cancel(millis());
    runSessionActions();
  }
  leave();
}

// Confirm only leaves the two screens that have nothing to offer: a radio that
// would not start, and a file that cannot be read.
void NearbyFileTransferActivity::onConfirmButton() {
  if (radioFailed || sourceUnreadable) leave();
}

bool NearbyFileTransferActivity::handleCustomInput() {
  if (radioFailed || sourceUnreadable) return false;

  pumpRadio();
  runSessionActions();

  const TransferState state = session.state();

  if (state == TransferState::TRANSFERRING) {
    const int percent = session.progressPercent();
    if (percent != lastDrawnPercent && millis() - lastProgressDrawMs >= PROGRESS_REDRAW_INTERVAL_MS) {
      lastProgressDrawMs = millis();
      lastDrawnPercent = percent;
      refreshProgressLines();
      requestUpdate();
    }
    return false;
  }

  // A finished transfer closes its files immediately rather than at exit, so the
  // last bytes are on the card before the screen says so.
  if (state == TransferState::DONE && destinationOpen) {
    incoming.flush();
    incoming.close();
    destinationOpen = false;
    // A credential bundle is not a file the reader keeps: it is read, applied, and
    // removed. Doing it the moment the bytes land means the passwords sit on the
    // card for as short a time as possible.
    if (mode == Mode::Receive && credential_bundle::isBundleFilename(destinationPath)) importCredentialBundle();
  }
  if (session.shouldDiscardPartialFile() && !destinationPath.empty()) discardPartialFile();

  if (state == TransferState::DONE && mode == Mode::Receive && group.expectsMore()) {
    group.onFileDone(millis());
    if (group.expectsMore()) {
      awaitNextGroupFile();
      return false;
    }
    if (!createdFamilyPath.empty()) {
      // The family is whole: let the font list pick it up without a reboot.
      createdFamilyPath.clear();
      sdFontSystem.markRegistryDirty();
    }
  }

  if (state == TransferState::DONE && mode == Mode::Send && sourceIndex + 1 < sourcePaths.size()) {
    advanceToNextSource();
    return false;
  }

  if (state == TransferState::REJECTED || state == TransferState::CANCELLED || state == TransferState::FAILED) {
    discardPartialFamily();
  }

  // A sender that walked out of range mid-family leaves nothing half-installed
  // and nothing waiting to be taken without a prompt.
  if (mode == Mode::Receive && group.isStale(millis())) {
    discardPartialFamily();
    group.reset();
  }

  if ((state == TransferState::DONE || state == TransferState::REJECTED || state == TransferState::CANCELLED ||
       state == TransferState::FAILED) &&
      autoReturnAt == 0) {
    autoReturnAt = millis() + AUTO_RETURN_DELAY_MS;
  }
  if (autoReturnAt != 0 && millis() >= autoReturnAt) {
    leave();
    return true;
  }

  // A reader that names itself after answering changes only its own row, which
  // none of the counters below would notice.
  const bool peersRenamed = state == TransferState::PEERS_FOUND && refreshPeerLabels();
  if (peersRenamed) requestUpdate();

  if (state != renderedState || session.peerCount() != renderedPeerCount || choiceIndex() != renderedSelection) {
    // Whatever changed, its text is rebuilt before the paint that shows it.
    if (state == TransferState::PEERS_FOUND) refreshPeerLabels();
    if (state == TransferState::OFFER_PROMPT) refreshOfferLines();
    if (state == TransferState::TRANSFERRING || state == TransferState::VERIFYING) refreshProgressLines();
    if (state == TransferState::DONE) refreshDoneLine();
    renderedState = state;
    renderedPeerCount = session.peerCount();
    renderedSelection = choiceIndex();
    requestUpdate();
  }
  return false;
}
