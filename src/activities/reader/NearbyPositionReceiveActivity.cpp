#include "NearbyPositionReceiveActivity.h"

#include <ChapterXPathResolver.h>
#include <Epub.h>
#include <Epub/Section.h>
#include <FsHelpers.h>
#include <I18n.h>
#include <KOReaderCredentialStore.h>
#include <KOReaderDocumentId.h>
#include <Logging.h>
#include <Memory.h>
#include <ProgressMapper.h>

#include "CrossPointSettings.h"
#include "NearbyPositionSession.h"
#include "ProgressFile.h"
#include "RecentBooksStore.h"
#include "activities/ActivityManager.h"
#include "util/BookCacheUtils.h"

using namespace nearby_position;

void NearbyPositionReceiveActivity::onEnter() {
  UiStatusActivity::onEnter();
  startedAt = millis();
  if (!link.begin()) setState(State::RadioBusy);
  requestUpdate(true);
}

void NearbyPositionReceiveActivity::closeScan() {
  for (auto& dir : directories) dir.close();
  for (auto& path : paths) std::string{}.swap(path);
  depth = -1;
}

void NearbyPositionReceiveActivity::onExit() {
  link.end();
  closeScan();
  Activity::onExit();
}

void NearbyPositionReceiveActivity::setState(State next) {
  {
    RenderLock lock(*this);
    state = next;
  }
  if (state != State::Matching) closeScan();
  requestUpdate();
}

bool NearbyPositionReceiveActivity::tryBook(const std::string& path) {
  if (!FsHelpers::hasEpubExtension(path)) return false;
  const std::string cache = bookCacheDirForPath(path);
  if (!Storage.exists((cache + "/book.bin").c_str()) || !Storage.exists((cache + "/progress.bin").c_str()) ||
      !Storage.exists(path.c_str()))
    return false;
  // Exactly documentHashFor's identity (NearbyPositionSyncActivity.cpp): honour
  // the same KOReader match setting without changing the working sender.
  const std::string hash = KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME
                               ? KOReaderDocumentId::calculateFromFilename(path)
                               : KOReaderDocumentId::calculate(path);
  if (!matchesDocumentHash(documentHash, hash)) return false;
  matchedPath = path;
  {
    RenderLock lock(*this);
    bookName = path.substr(path.find_last_of('/') + 1);
  }
  LOG_INF("NBPR", "Matched %s after %lu ms", path.c_str(), millis() - startedAt);
  startedAt = millis();
  setState(State::Waiting);
  reply();
  return true;
}

void NearbyPositionReceiveActivity::scanNext() {
  // Recents supplies paths for up to 13 opened books, without a card walk.
  const auto& recent = RECENT_BOOKS.getBooks();
  if (recentIndex < recent.size()) {
    tryBook(recent[recentIndex++].path);
    return;
  }
  // Cache directory names hash a path and book.bin does not store that path.
  // Walk directory entries, but hash contents ONLY for books with existing caches.
  if (directoriesSeen == 0) {
    directories[0] = Storage.open("/");
    if (!directories[0] || !directories[0].isDirectory()) {
      setState(State::Unavailable);
      return;
    }
    paths[0].clear();
    depth = 0;
    directoriesSeen = 1;
  }
  if (depth < 0) {
    setState(State::NoMatch);
    return;
  }
  auto entry = directories[depth].openNextFile();
  if (!entry) {
    directories[depth].close();
    --depth;
    return;
  }
  const size_t length = entry.getName(entryName, sizeof(entryName));
  if (length == 0 || length >= sizeof(entryName)) {
    setState(State::Unavailable);
    return;
  }
  if (entryName[0] == '.') return;
  const std::string path = paths[depth] + "/" + entryName;
  if (path.size() > 1024) {
    setState(State::Unavailable);
    return;
  }
  if (entry.isDirectory()) {
    if (depth == MAX_DEPTH || ++directoriesSeen > 4000) {
      setState(State::Unavailable);
      return;
    }
    ++depth;
    paths[depth] = path;
    directories[depth] = std::move(entry);
  } else {
    tryBook(path);
  }
}

bool NearbyPositionReceiveActivity::applyPosition(const PacketView& packet) {
  if (!receivablePosition(packet, documentHash)) return false;
  const std::string cache = bookCacheDirForPath(matchedPath);
  uint8_t bytes[10];
  std::optional<CachedPosition> local;
  {
    auto file = Storage.open((cache + "/progress.bin").c_str());
    if (!file || file.size() > sizeof(bytes)) return false;
    const size_t size = file.size();
    if (file.read(bytes, size) != static_cast<int>(size)) return false;
    local = readCachedPosition(bytes, size);
  }
  if (!local) return false;

  // One matched metadata object only; never build a book or load its CSS while
  // the radio is live. Heap allocation avoids putting Epub/Section on the task stack.
  auto epub = makeUniqueNoThrow<Epub>(matchedPath, "/.crosspoint");
  if (!epub) {
    LOG_ERR("NBPR", "OOM: Epub");
    return false;
  }
  if (!epub->load(false, true) || local->spine >= epub->getSpineItemsCount()) return false;
  auto section = makeUniqueNoThrow<::Section>(*epub, local->spine, renderer);
  if (!section) {
    LOG_ERR("NBPR", "OOM: Section");
    return false;
  }
  if (local->spine == 0 && epub->getSpineIndexForTextReference() != 0) return false;
  const bool localSectionMissing =
      !Storage.exists((cache + "/sections/" + std::to_string(local->spine) + ".bin").c_str());
  if (localSectionMissing && local->offset) {
    // A previous receive may have saved an offset before this chapter was ever
    // opened. The reader will rebuild by offset; verify it against the text so
    // another receive can still keep the furthest place without opening a book.
    if (ChapterXPathResolver::findXPathForVisibleTextOffset(*epub, local->spine, *local->offset).empty()) return false;
  } else {
    const auto count = section->getCachedPageCount();
    const auto pageOffset = section->getVisibleTextOffsetForPage(local->page);
    if (!pageOffset) return false;
    if (local->offset) {
      // Prove the saved page and offset agree with this cache before replacing it.
      if (section->getPageForVisibleTextOffset(*local->offset) != local->page) return false;
    } else if (!count || local->pages != *count) {
      return false;  // old page-only record from an unknown layout
    } else {
      local->offset = pageOffset;
    }
  }
  section.reset();

  SavedProgressPosition saved{packet.position.xpath.data(), percentageFromQ(packet.position.percentageQ)};
  const CrossPointPosition mapped = ProgressMapper::toCrossPoint(*epub, saved, renderer);
  if (!mapped.hasResolvedSpineIndex || !mapped.hasVisibleTextOffset || mapped.spineIndex < 0 ||
      mapped.spineIndex >= epub->getSpineItemsCount())
    return false;
  // On reopen the reader redirects spine zero to the EPUB's text reference.
  if (mapped.spineIndex == 0 && epub->getSpineIndexForTextReference() != 0) return false;
  // The mapper has permissive fallbacks. Accept only an anchor that round-trips
  // exactly, so truncation, relaxed ancestry and percentage guesses cannot save.
  const bool verified = ChapterXPathResolver::findXPathForVisibleTextOffset(*epub, mapped.spineIndex,
                                                                            mapped.visibleTextOffset) == saved.xpath;
  section = makeUniqueNoThrow<::Section>(*epub, mapped.spineIndex, renderer);
  if (!section) {
    LOG_ERR("NBPR", "OOM: Section");
    return false;
  }
  const auto targetCount = section->getCachedPageCount();
  const auto targetPage = section->getPageForVisibleTextOffset(mapped.visibleTextOffset);
  const bool sectionMissing =
      !Storage.exists((cache + "/sections/" + std::to_string(mapped.spineIndex) + ".bin").c_str());
  const CachedPosition target{static_cast<uint16_t>(mapped.spineIndex), targetPage.value_or(0), targetCount.value_or(0),
                              mapped.visibleTextOffset};
  const auto record =
      receivedProgressRecord(std::string_view(packet.position.documentHash.data(), DOCUMENT_HASH_BYTES), documentHash,
                             target, epub->getSpineItemsCount(), verified, targetPage.has_value(), sectionMissing);
  if (!record) return false;
  const Resolution resolution = resolveCachedPosition(*local, target);
  if (resolution == Resolution::TakePeer) {
    if (!ProgressFile::writeAtomic(cache, record->data(), record->size())) {
      setState(State::Failed);
      return false;
    }
    setState(State::Applied);
  } else {
    setState(resolution == Resolution::Same ? State::Same : State::Kept);
  }
  return true;
}

void NearbyPositionReceiveActivity::reply() {
  const std::string name = SETTINGS.getEffectiveDeviceName();
  if (state == State::Waiting) {
    link.send(PacketType::HELLO, peerMac, peerPosition, name);
    link.send(PacketType::NAME, peerMac, peerPosition, name);
  } else if (isComplete()) {
    // ACK only after a successful save/keep decision. Echo their exact position
    // to complete the existing symmetric exchange without moving the sender.
    link.send(PacketType::ACK, peerMac, peerPosition, name);
    link.send(PacketType::POSITION, peerMac, peerPosition, name);
  }
  lastReplyAt = millis();
}

bool NearbyPositionReceiveActivity::handleCustomInput() {
  if (state == State::RadioBusy || state == State::NoMatch || state == State::Unavailable || state == State::Failed ||
      state == State::TimedOut)
    return false;
  while (link.nextReceived(received)) {
    const auto& packet = received.packet;
    if (received.sourceMac != packet.deviceMac || received.sourceMac == link.localMac()) continue;
    if (state == State::Searching) {
      if (packet.type != PacketType::HELLO && packet.type != PacketType::POSITION && packet.type != PacketType::APPLY)
        continue;
      const std::string_view hash(packet.position.documentHash.data(), DOCUMENT_HASH_BYTES);
      if (!matchesDocumentHash(hash, hash)) continue;
      peerMac = received.sourceMac;
      documentHash.assign(hash);
      peerPosition = packet.position;
      startedAt = millis();
      setState(State::Matching);
    }
    if (received.sourceMac != peerMac) continue;
    if (isComplete() && packet.type == PacketType::ACK) {
      replyAcknowledged = true;
      continue;
    }
    if (!receivablePosition(packet, documentHash)) continue;
    if (isComplete() && packet.position.xpath == peerPosition.xpath &&
        packet.position.percentageQ == peerPosition.percentageQ) {
      reply();  // their ACK may have been lost; never write the record twice
    }
    if (state == State::Waiting) {
      if (!applyPosition(packet)) {
        if (state != State::Failed) setState(State::Unavailable);
        LOG_ERR("NBPR", "Refused unsafe/unavailable cached position for %s", matchedPath.c_str());
        break;
      }
      peerPosition = packet.position;
      reply();
    }
  }
  if (state == State::Matching) scanNext();  // one entry/hash per loop: Back stays responsive
  if ((state == State::Searching || state == State::Matching || state == State::Waiting) &&
      millis() - startedAt >= SEARCH_TIMEOUT_MS) {
    setState(state == State::Matching ? State::Unavailable : State::TimedOut);
  }
  if ((state == State::Waiting || (isComplete() && !replyAcknowledged)) &&
      millis() - lastReplyAt >= POSITION_RETRY_INTERVAL_MS)
    reply();
  return false;
}

UiStatusActivity::StatusView NearbyPositionReceiveActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_NEARBY_RECEIVE_POSITION);
  const char* message = tr(STR_NEARBY_WAITING_TO_RECEIVE);
  switch (state) {
    case State::Searching:
      break;
    case State::Matching:
      message = tr(STR_NEARBY_MATCHING_BOOK);
      break;
    case State::Waiting:
      message = tr(STR_NEARBY_WAITING_TO_RECEIVE);
      break;
    case State::Applied:
      message = tr(STR_NEARBY_POSITION_SAVED);
      break;
    case State::Kept:
      message = tr(STR_NEARBY_KEPT_POSITION);
      break;
    case State::Same:
      message = tr(STR_NEARBY_SAME_PAGE);
      break;
    case State::NoMatch:
      message = tr(STR_NEARBY_NO_CACHED_BOOK);
      break;
    case State::Unavailable:
      message = tr(STR_NEARBY_POSITION_UNAVAILABLE);
      break;
    case State::Failed:
      message = tr(STR_NEARBY_CANNOT_WRITE_FILE);
      break;
    case State::TimedOut:
      message = tr(STR_NEARBY_TIMED_OUT);
      break;
    case State::RadioBusy:
      message = tr(STR_NEARBY_RADIO_BUSY);
      break;
  }
  view.sections[0].heading = message;
  view.sections[0].paragraph = bookName.empty() ? nullptr : bookName.c_str();
  if (state == State::Searching) view.sections[0].paragraph = tr(STR_NEARBY_RECEIVE_POSITION_HINT);
  return view;
}

void NearbyPositionReceiveActivity::onBackButton() { activityManager.goHome(); }
void NearbyPositionReceiveActivity::onConfirmButton() {
  if (state != State::Searching && state != State::Matching && state != State::Waiting) onBackButton();
}
