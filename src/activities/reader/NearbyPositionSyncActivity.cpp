#include "NearbyPositionSyncActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <utility>

#include "CrossPointSettings.h"
#include "EpubReaderUtils.h"
#include "I18nKeys.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderDocumentId.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"

using namespace nearby_position;

namespace {

constexpr const char* LOG_TAG = "NBPS";

std::string documentHashFor(const std::string& path) {
  // The same identity KOSync uses, so a book matched by one is matched by the
  // other, and a CrossInk device computing it the same way still pairs.
  return KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME
             ? KOReaderDocumentId::calculateFromFilename(path)
             : KOReaderDocumentId::calculate(path);
}

}  // namespace

NearbyPositionSyncActivity::NearbyPositionSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                       const std::string& epubPath, const int currentSpineIndex,
                                                       const int currentPage, const int totalPagesInSpine,
                                                       SavedProgressPosition localProgress,
                                                       std::optional<uint16_t> currentParagraphIndex)
    : UiStatusActivity("NearbyPositionSync", renderer, mappedInput),
      epubPath(epubPath),
      currentSpineIndex(currentSpineIndex),
      currentPage(currentPage),
      totalPagesInSpine(totalPagesInSpine < 1 ? 1 : totalPagesInSpine),
      currentParagraphIndex(currentParagraphIndex),
      localProgress(std::move(localProgress)) {}

bool NearbyPositionSyncActivity::prepareLocalPosition() {
  documentHash = documentHashFor(epubPath);
  if (documentHash.size() != DOCUMENT_HASH_BYTES || localProgress.xpath.empty()) {
    LOG_ERR(LOG_TAG, "No shareable position for %s", epubPath.c_str());
    return false;
  }

  CompactPosition position;
  setDocumentHash(position, documentHash);
  setXpath(position, localProgress.xpath);
  position.percentageQ = percentageToQ(localProgress.percentage);
  position.spineIndex = static_cast<uint16_t>(currentSpineIndex < 0 ? 0 : currentSpineIndex);
  position.pageNumber = static_cast<uint16_t>(currentPage < 0 ? 0 : currentPage);
  position.totalPages = static_cast<uint16_t>(totalPagesInSpine);
  if (currentParagraphIndex.has_value() && *currentParagraphIndex != UINT16_MAX) {
    position.paragraphIndex = *currentParagraphIndex;
    position.hasParagraphIndex = true;
  }

  session.begin(position, millis());
  return true;
}

void NearbyPositionSyncActivity::onEnter() {
  UiStatusActivity::onEnter();

  if (!prepareLocalPosition()) {
    noLocalPosition = true;
    requestUpdate(true);
    return;
  }

  if (!link.begin()) {
    radioFailed = true;
    requestUpdate(true);
    return;
  }
  session.setLocalMac(link.localMac());
  requestUpdate(true);
}

void NearbyPositionSyncActivity::onExit() {
  link.end();
  Activity::onExit();
}

void NearbyPositionSyncActivity::ensureEpubLoaded() {
  if (epub) return;

  epub = makeUniqueNoThrow<Epub>(epubPath, "/.crosspoint");
  if (!epub) {
    LOG_ERR("NBPS", "OOM: Epub");
    return;
  }
  epub->setupCacheDir();
  // Metadata only: mapping a position needs the spine and TOC, not the CSS, and
  // this must not rebuild a missing cache while the radio holds the heap.
  if (!epub->load(false, true)) {
    LOG_ERR(LOG_TAG, "Could not load epub for position mapping");
    epub.reset();
  }
}

void NearbyPositionSyncActivity::pumpRadio() {
  EspNowLink::Received received;
  while (link.nextReceived(received)) {
    session.onPacket(received.packet, millis());
  }
}

void NearbyPositionSyncActivity::runSessionActions() {
  const std::string deviceName = SETTINGS.getEffectiveDeviceName();

  Action action;
  while (session.nextAction(millis(), action)) {
    switch (action.kind) {
      case ActionKind::BROADCAST_HELLO:
        link.broadcast(PacketType::HELLO, session.localPosition(), deviceName);
        break;
      case ActionKind::SEND_NAME:
        link.send(PacketType::NAME, action.peerMac, session.localPosition(), deviceName);
        break;
      case ActionKind::SEND_POSITION:
        link.send(PacketType::POSITION, action.peerMac, session.localPosition(), deviceName);
        break;
      case ActionKind::SEND_ACK:
        link.send(PacketType::ACK, action.peerMac, session.localPosition(), deviceName);
        break;
    }
  }
}

bool NearbyPositionSyncActivity::applyPeerPosition() {
  ensureEpubLoaded();
  if (!epub) {
    return false;
  }

  const CompactPosition& peer = session.peerPosition();
  SavedProgressPosition saved;
  saved.xpath = peer.xpath.data();
  saved.percentage = percentageFromQ(peer.percentageQ);

  // The xpath is the authoritative anchor; the spine and page it arrived with
  // are only hints for estimating where it lands in this device's layout, which
  // may be paginated differently.
  const CrossPointPosition mapped =
      ProgressMapper::toCrossPoint(*epub, saved, renderer, currentSpineIndex, totalPagesInSpine, totalPagesInSpine);

  std::optional<uint32_t> offset;
  if (mapped.hasVisibleTextOffset) offset = mapped.visibleTextOffset;
  if (!EpubReaderUtils::saveProgress(*epub, mapped.spineIndex, mapped.pageNumber, 0, offset)) {
    LOG_ERR(LOG_TAG, "Could not save the received position");
    return false;
  }
  return true;
}

void NearbyPositionSyncActivity::returnToReader() {
  link.end();
  activityManager.goToReader(epubPath);
}

UiStatusActivity::StatusView NearbyPositionSyncActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_NEARBY_SYNC);
  if (saveFailed) {
    view.lines = {tr(STR_NEARBY_CANNOT_WRITE_FILE), nullptr, nullptr, nullptr};
    return view;
  }
  if (noLocalPosition) {
    view.lines = {tr(STR_NEARBY_NO_POSITION), nullptr, nullptr, nullptr};
    return view;
  }
  if (radioFailed) {
    view.lines = {tr(STR_NEARBY_RADIO_BUSY), nullptr, nullptr, nullptr};
    return view;
  }

  const SyncState state = session.state();
  switch (state) {
    case SyncState::SEARCHING:
      view.lines = {tr(STR_NEARBY_SEARCHING), tr(STR_NEARBY_SEARCHING_HINT), nullptr, nullptr};
      break;
    case SyncState::EXCHANGING:
      view.lines = {tr(STR_NEARBY_SENDING), nullptr, nullptr, nullptr};
      break;
    case SyncState::SHARED:
      view.lines = {session.positionsMatch() ? tr(STR_NEARBY_SAME_PAGE) : tr(STR_NEARBY_KEPT_POSITION), nullptr,
                    nullptr, nullptr};
      break;
    case SyncState::APPLIED:
      view.sections[0].paragraph = tr(STR_NEARBY_MOVED_TO_PEER);
      break;
    case SyncState::BOOK_MISMATCH:
      view.lines = {tr(STR_NEARBY_BOOK_MISMATCH), nullptr, nullptr, nullptr};
      break;
    case SyncState::PEER_LOST:
      view.lines = {tr(STR_NEARBY_PEER_LOST), nullptr, nullptr, nullptr};
      break;
    case SyncState::TIMED_OUT:
      view.lines = {tr(STR_NEARBY_TIMED_OUT), nullptr, nullptr, nullptr};
      break;
  }
  return view;
}

bool NearbyPositionSyncActivity::handleCustomInput() {
  // Failure screens wait for a button, which the base reads.
  if (noLocalPosition || radioFailed || saveFailed) return false;

  pumpRadio();
  runSessionActions();

  const SyncState state = session.state();

  if ((state == SyncState::SHARED || state == SyncState::APPLIED) && autoReturnAt == 0) {
    if (state == SyncState::APPLIED && !applyPeerPosition()) saveFailed = true;
    autoReturnAt = millis() + AUTO_RETURN_DELAY_MS;
  }
  if (!saveFailed && autoReturnAt != 0 && millis() >= autoReturnAt) {
    returnToReader();
    return true;
  }

  if (state != renderedState) {
    // The radio counts are worth one line when a sync ends badly: they say
    // whether nothing was heard, whether what arrived was not this protocol, and
    // who sent it. The readers are used away from a serial cable, so this is
    // read back from the log after the fact.
    if (state != renderedState && (state == SyncState::TIMED_OUT || state == SyncState::PEER_LOST)) {
      LOG_INF(LOG_TAG, "Sync ended: sent %u heard %u undecoded %u, self %u peer %u other %u", link.framesSent(),
              link.framesHeard(), link.framesNotDecoded(), session.packetsFromSelf(), session.packetsFromPeer(),
              session.packetsFromOthers());
    }
    renderedState = state;
    requestUpdate();
  }
  return false;
}

void NearbyPositionSyncActivity::onBackButton() { returnToReader(); }

// Confirm dismisses a failure; successful sync returns automatically.
void NearbyPositionSyncActivity::onConfirmButton() {
  if (noLocalPosition || radioFailed || saveFailed) returnToReader();
}
