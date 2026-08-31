#include "NearbyPositionSyncActivity.h"

#include <I18n.h>
#include <Logging.h>

#include <cstdio>
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
                                                       std::string localChapterName,
                                                       std::optional<uint16_t> currentParagraphIndex)
    : UiStatusActivity("NearbyPositionSync", renderer, mappedInput),
      epubPath(epubPath),
      localChapterName(std::move(localChapterName)),
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

  epub = std::make_shared<Epub>(epubPath, "/.crosspoint");
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
      case ActionKind::SEND_APPLY:
        link.send(PacketType::APPLY, action.peerMac, session.localPosition(), deviceName);
        break;
    }
  }
}

void NearbyPositionSyncActivity::applyPeerPosition() {
  ensureEpubLoaded();
  if (!epub) {
    returnToReader();
    return;
  }

  const CompactPosition& peer = session.peerPosition();
  SavedProgressPosition saved;
  saved.xpath = peer.xpath.data();
  saved.percentage = percentageFromQ(peer.percentageQ);

  // The xpath is the authoritative anchor; the spine and page it arrived with
  // are only hints for estimating where it lands in this device's layout, which
  // may be paginated differently.
  const CrossPointPosition mapped =
      ProgressMapper::toCrossPoint(epub, saved, renderer, currentSpineIndex, totalPagesInSpine, totalPagesInSpine);

  std::optional<uint32_t> offset;
  if (mapped.hasVisibleTextOffset) offset = mapped.visibleTextOffset;
  if (!EpubReaderUtils::saveProgress(*epub, mapped.spineIndex, mapped.pageNumber, 0, offset)) {
    LOG_ERR(LOG_TAG, "Could not save the received position");
  }
  returnToReader();
}

void NearbyPositionSyncActivity::returnToReader() {
  link.end();
  activityManager.goToReader(epubPath);
}

std::string NearbyPositionSyncActivity::chapterNameFor(const int spineIndex) const {
  if (epub) {
    const int tocIndex = epub->getTocIndexForSpineIndex(spineIndex);
    if (tocIndex >= 0) return epub->getTocItem(tocIndex).title;
  }
  return std::string(tr(STR_SECTION_PREFIX)) + std::to_string(spineIndex + 1);
}

void NearbyPositionSyncActivity::prepareComparison() {
  peerChapterLine = chapterNameFor(peerLocalPosition.spineIndex);
  localChapterLine = !localChapterName.empty()
                         ? localChapterName
                         : (std::string(tr(STR_SECTION_PREFIX)) + std::to_string(currentSpineIndex + 1));

  char buffer[128];
  std::snprintf(buffer, sizeof(buffer), tr(STR_PAGE_OVERALL_FORMAT), peerLocalPosition.pageNumber + 1,
                percentageFromQ(session.peerPosition().percentageQ) * 100.0f);
  peerPageLine = buffer;
  if (!session.peerName().empty()) {
    std::snprintf(buffer, sizeof(buffer), tr(STR_DEVICE_FROM_FORMAT), session.peerName().c_str());
    peerDeviceLine = buffer;
  } else {
    peerDeviceLine.clear();
  }

  std::snprintf(buffer, sizeof(buffer), tr(STR_PAGE_TOTAL_OVERALL_FORMAT), currentPage + 1, totalPagesInSpine,
                localProgress.percentage * 100.0f);
  localPageLine = buffer;
}

UiStatusActivity::StatusView NearbyPositionSyncActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_NEARBY_SYNC);
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
    case SyncState::COMPARING:
    case SyncState::APPLY_REQUESTED:
      // The same comparison either way; the headline says whether the other
      // reader is asking for this device's page or simply offering its own.
      view.comparisonHeadline = state == SyncState::APPLY_REQUESTED ? tr(STR_NEARBY_INCOMING) : tr(STR_NEARBY_FOUND);
      view.comparison[0].label = tr(STR_NEARBY_THEIRS);
      view.comparison[0].lines = {peerChapterLine.c_str(), peerPageLine.c_str(),
                                  peerDeviceLine.empty() ? nullptr : peerDeviceLine.c_str()};
      view.comparison[1].label = tr(STR_NEARBY_MINE);
      view.comparison[1].lines = {localChapterLine.c_str(), localPageLine.c_str(), nullptr};
      // Say plainly which side is ahead, so the choice does not rest on
      // comparing two percentages by eye.
      view.comparisonRelation = session.positionsMatch()       ? tr(STR_NEARBY_SAME_PAGE)
                                : session.peerIsFurtherAlong() ? tr(STR_NEARBY_FURTHER_AHEAD)
                                                               : nullptr;
      view.choices = {tr(STR_NEARBY_TAKE_THEIRS), tr(STR_NEARBY_SEND_MINE)};
      view.confirmHint = tr(STR_SELECT);
      break;
    case SyncState::SHARING:
      view.lines = {tr(STR_NEARBY_SENDING), nullptr, nullptr, nullptr};
      break;
    case SyncState::SHARED:
      view.lines = {tr(STR_NEARBY_SENT), nullptr, nullptr, nullptr};
      break;
    case SyncState::APPLIED:
      view.lines = {tr(STR_NEARBY_APPLIED), nullptr, nullptr, nullptr};
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
  // Nothing left to run: the two failure screens only wait for a button, which
  // the base reads.
  if (noLocalPosition || radioFailed) return false;

  if (autoReturnAt != 0) {
    if (millis() >= autoReturnAt) returnToReader();
    return true;
  }

  pumpRadio();
  runSessionActions();

  const SyncState state = session.state();

  // Map the peer's position once it is known, so the comparison can name their
  // chapter rather than only a raw section number.
  if (session.hasPeerPosition() && !peerPositionMapped) {
    ensureEpubLoaded();
    if (epub) {
      SavedProgressPosition peerSaved;
      peerSaved.xpath = session.peerPosition().xpath.data();
      peerSaved.percentage = percentageFromQ(session.peerPosition().percentageQ);
      peerLocalPosition = ProgressMapper::toCrossPoint(epub, peerSaved, renderer, currentSpineIndex, totalPagesInSpine,
                                                       totalPagesInSpine);
    } else {
      // Nothing to resolve the xpath against, and toCrossPoint() dereferences the
      // book on its first line. Show the raw numbers the peer sent instead.
      peerLocalPosition.spineIndex = session.peerPosition().spineIndex;
      peerLocalPosition.pageNumber = session.peerPosition().pageNumber;
      peerLocalPosition.totalPages = session.peerPosition().totalPages;
    }
    peerPositionMapped = true;
    prepareComparison();
  }

  // The peer names itself in its own packet, which can land after its position:
  // rebuild the line and repaint when it does, or the comparison keeps saying
  // nothing about who is on the other end.
  if (peerPositionMapped && session.peerName() != renderedPeerName) {
    renderedPeerName = session.peerName();
    prepareComparison();
    requestUpdate();
  }

  // The peer took our page, or the search ended: show the outcome briefly rather
  // than snapping straight back into the book.
  if (state == SyncState::SHARED && autoReturnAt == 0) autoReturnAt = millis() + AUTO_RETURN_DELAY_MS;

  if (state != renderedState || session.hasPeerPosition() != renderedPeerPosition || choiceIndex() != renderedChoice) {
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
    renderedPeerPosition = session.hasPeerPosition();
    renderedChoice = choiceIndex();
    requestUpdate();
  }
  return false;
}

void NearbyPositionSyncActivity::onBackButton() { returnToReader(); }

// Confirm leaves only the two screens that have nothing else to offer: a device
// with no position to share, and a radio that would not start.
void NearbyPositionSyncActivity::onConfirmButton() {
  if (noLocalPosition || radioFailed) returnToReader();
}

void NearbyPositionSyncActivity::onChoiceActivated(const int index) {
  const SyncState state = session.state();
  if (state != SyncState::COMPARING && state != SyncState::APPLY_REQUESTED) return;
  if (index == TAKE_THEIRS) {
    session.takePeerPosition(millis());
    applyPeerPosition();
    return;
  }
  session.sharePosition(millis());
  requestUpdate();
}
