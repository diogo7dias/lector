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
#include "components/UITheme.h"
#include "fontIds.h"

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
    : Activity("NearbyPositionSync", renderer, mappedInput),
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
  Activity::onEnter();

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

void NearbyPositionSyncActivity::loop() {
  if (noLocalPosition || radioFailed) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      returnToReader();
    }
    return;
  }

  if (autoReturnAt != 0) {
    if (millis() >= autoReturnAt) returnToReader();
    return;
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
  }

  if (state == SyncState::COMPARING || state == SyncState::APPLY_REQUESTED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Down) ||
        mappedInput.wasPressed(MappedInputManager::Button::Left) ||
        mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      choice = choice == Choice::TAKE_THEIRS ? Choice::SEND_MINE : Choice::TAKE_THEIRS;
      requestUpdate();
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (choice == Choice::TAKE_THEIRS) {
        session.takePeerPosition(millis());
        applyPeerPosition();
      } else {
        session.sharePosition(millis());
        requestUpdate();
      }
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    returnToReader();
    return;
  }

  // The peer took our page, or the search ended: show the outcome briefly rather
  // than snapping straight back into the book.
  if (state == SyncState::SHARED && autoReturnAt == 0) autoReturnAt = millis() + AUTO_RETURN_DELAY_MS;

  if (state != renderedState || session.hasPeerPosition() != renderedPeerPosition || choice != renderedChoice) {
    // The radio counts are worth one line when a sync ends badly: they say
    // whether nothing was heard, whether what arrived was not this protocol, and
    // who sent it. The readers are used away from a serial cable, so this is
    // read back from the log after the fact.
    if (state != renderedState && (state == SyncState::TIMED_OUT || state == SyncState::PEER_LOST)) {
      LOG_INF(LOG_TAG, "Sync ended: sent %u heard %u undecoded %u, self %u peer %u other %u", link.framesSent(),
              link.framesHeard(), link.framesNotDecoded(), session.packetsFromSelf(), session.packetsFromPeer(),
              session.packetsFromOthers());
    }
    requestUpdate();
  }
}

Rect NearbyPositionSyncActivity::detailBounds(const Rect& screen, const int top) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = std::max(1, screen.width - metrics.contentSidePadding * 2);
  // Inset by the side padding, then wrapped inside that width. A whole hint
  // sentence is wider than this screen, and unwrapped centred text runs off both
  // edges instead of breaking.
  return Rect{screen.x + metrics.contentSidePadding, top, width,
              renderer.getLineHeight(UI_10_FONT_ID) * DETAIL_MAX_LINES};
}

void NearbyPositionSyncActivity::renderSearching(const Rect& screen, const int top) const {
  UITheme::drawCenteredText(renderer, screen, UI_10_FONT_ID, top, tr(STR_NEARBY_SEARCHING), true,
                            EpdFontFamily::REGULAR);
  UITheme::drawCenteredWrappedText(renderer, detailBounds(screen, top + 40), UI_10_FONT_ID,
                                   tr(STR_NEARBY_SEARCHING_HINT), DETAIL_MAX_LINES, true, EpdFontFamily::REGULAR,
                                   UITheme::TextVerticalAlignment::TOP);
}

void NearbyPositionSyncActivity::renderMessage(const Rect& screen, const int top, const char* message,
                                               const char* detail) const {
  UITheme::drawCenteredText(renderer, screen, UI_10_FONT_ID, top, message, true, EpdFontFamily::REGULAR);
  if (detail && detail[0] != '\0') {
    UITheme::drawCenteredWrappedText(renderer, detailBounds(screen, top + 40), UI_10_FONT_ID, detail, DETAIL_MAX_LINES,
                                     true, EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
  }
}

void NearbyPositionSyncActivity::renderComparison(const Rect& screen, const int top) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int left = screen.x + metrics.contentSidePadding;

  renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_NEARBY_FOUND), true, EpdFontFamily::REGULAR);

  const std::string peerChapter = chapterNameFor(peerLocalPosition.spineIndex);
  const std::string localChapter = !localChapterName.empty()
                                       ? localChapterName
                                       : (std::string(tr(STR_SECTION_PREFIX)) + std::to_string(currentSpineIndex + 1));

  renderer.drawText(UI_10_FONT_ID, left, top + 40, tr(STR_NEARBY_THEIRS), true);
  char buffer[128];
  std::snprintf(buffer, sizeof(buffer), "  %s", peerChapter.c_str());
  renderer.drawText(UI_10_FONT_ID, left, top + 65, buffer);
  std::snprintf(buffer, sizeof(buffer), tr(STR_PAGE_OVERALL_FORMAT), peerLocalPosition.pageNumber + 1,
                percentageFromQ(session.peerPosition().percentageQ) * 100.0f);
  renderer.drawText(UI_10_FONT_ID, left, top + 90, buffer);
  if (!session.peerName().empty()) {
    std::snprintf(buffer, sizeof(buffer), tr(STR_DEVICE_FROM_FORMAT), session.peerName().c_str());
    renderer.drawText(UI_10_FONT_ID, left, top + 115, buffer);
  }

  renderer.drawText(UI_10_FONT_ID, left, top + 150, tr(STR_NEARBY_MINE), true);
  std::snprintf(buffer, sizeof(buffer), "  %s", localChapter.c_str());
  renderer.drawText(UI_10_FONT_ID, left, top + 175, buffer);
  std::snprintf(buffer, sizeof(buffer), tr(STR_PAGE_TOTAL_OVERALL_FORMAT), currentPage + 1, totalPagesInSpine,
                localProgress.percentage * 100.0f);
  renderer.drawText(UI_10_FONT_ID, left, top + 200, buffer);

  // Say plainly which side is ahead, so the choice does not rest on comparing
  // two percentages by eye.
  const char* relation = session.positionsMatch()       ? tr(STR_NEARBY_SAME_PAGE)
                         : session.peerIsFurtherAlong() ? tr(STR_NEARBY_FURTHER_AHEAD)
                                                        : "";
  if (relation[0] != '\0') renderer.drawText(UI_10_FONT_ID, left, top + 225, relation);

  const int optionY = top + 255;
  const int optionHeight = 30;

  if (choice == Choice::TAKE_THEIRS) {
    renderer.fillRect(screen.x, optionY - 2, screen.width - 1, optionHeight);
  }
  renderer.drawText(UI_10_FONT_ID, left, optionY, tr(STR_NEARBY_TAKE_THEIRS), choice != Choice::TAKE_THEIRS);

  if (choice == Choice::SEND_MINE) {
    renderer.fillRect(screen.x, optionY + optionHeight - 2, screen.width - 1, optionHeight);
  }
  renderer.drawText(UI_10_FONT_ID, left, optionY + optionHeight, tr(STR_NEARBY_SEND_MINE), choice != Choice::SEND_MINE);
}

void NearbyPositionSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_NEARBY_SYNC));

  const int centred = screen.y + screen.height / 2 - 40;
  const int listTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  const SyncState state = session.state();
  renderedState = state;
  renderedPeerPosition = session.hasPeerPosition();
  renderedChoice = choice;

  bool showChoiceHints = false;
  if (noLocalPosition) {
    renderMessage(screen, centred, tr(STR_NEARBY_NO_POSITION), "");
  } else if (radioFailed) {
    renderMessage(screen, centred, tr(STR_NEARBY_RADIO_BUSY), "");
  } else {
    switch (state) {
      case SyncState::SEARCHING:
        renderSearching(screen, centred);
        break;
      case SyncState::COMPARING:
        renderComparison(screen, listTop);
        showChoiceHints = true;
        break;
      case SyncState::APPLY_REQUESTED:
        renderer.drawCenteredText(UI_10_FONT_ID, listTop - metrics.verticalSpacing, tr(STR_NEARBY_INCOMING), true,
                                  EpdFontFamily::REGULAR);
        renderComparison(screen, listTop);
        showChoiceHints = true;
        break;
      case SyncState::SHARING:
        renderMessage(screen, centred, tr(STR_NEARBY_SENDING), "");
        break;
      case SyncState::SHARED:
        renderMessage(screen, centred, tr(STR_NEARBY_SENT), "");
        break;
      case SyncState::APPLIED:
        renderMessage(screen, centred, tr(STR_NEARBY_APPLIED), "");
        break;
      case SyncState::BOOK_MISMATCH:
        renderMessage(screen, centred, tr(STR_NEARBY_BOOK_MISMATCH), "");
        break;
      case SyncState::PEER_LOST:
        renderMessage(screen, centred, tr(STR_NEARBY_PEER_LOST), "");
        break;
      case SyncState::TIMED_OUT:
        renderMessage(screen, centred, tr(STR_NEARBY_TIMED_OUT), "");
        break;
    }
  }

  const auto labels = showChoiceHints
                          ? mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN))
                          : mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
