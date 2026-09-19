#include "FontDownloadActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_rom_crc.h>

#include <algorithm>

#include "Diagnostics.h"
#include "MappedInputManager.h"
#include "Memory.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HeapFailureProbe.h"
#include "network/HttpDownloader.h"
#include "network/TlsHeapPolicy.h"
#include "network/TlsScratchHeap.h"

namespace {

// Downloaded once per visit and kept until onExit: a font download re-reads one
// family's file names from it rather than holding every family's names in RAM.
constexpr const char* MANIFEST_TMP = "/fonts_manifest.tmp";

// What to put on the screen for a transfer that did not finish. The reader
// cannot open a serial log, so the difference between "nothing answered",
// "the server said no" and "the card would not take it" has to reach the
// panel: each one needs a different thing done about it.
// tr() pastes StrId:: onto a bare key name, so a StrId that was chosen at
// runtime cannot go through it.
const char* trId(const StrId id) { return I18n::getInstance().get(id); }

// Everything known about a transfer that did not finish, gathered the moment it
// gave up rather than read off the heap afterwards.
//
// HttpDownloader::NO_CONNECTION means only "no status line was ever read". That
// covers a wrong access point, no route, DNS, a refused socket, a TLS alert --
// AND a handshake that could not allocate what it needed. Those want opposite
// things done about them, so the screen may not call them all "could not reach
// the font server": heap_probe counts the allocations that actually failed
// during the transfer, whoever asked for them, and that is what separates the
// two.
struct TransferFailure {
  HttpDownloader::DownloadError error = HttpDownloader::OK;
  int httpStatus = 0;
  bool framebufferLent = true;
  heap_probe::Record allocations;  // heap allocations that failed during the transfer
  uint32_t loanFallbacks = 0;      // wolfSSL allocations the lent framebuffer could not serve
  uint32_t loanFallbackBytes = 0;  // what those came to, which is how much bigger the loan needs to be
  uint32_t loanLowWater = 0;       // least the lent block ever held, read while it was still registered
  uint32_t tlsOoms = 0;            // wolfSSL allocations that came back null -- this is MEMORY_E (-125)
  uint32_t tlsOomSize = 0;         // bytes the last of them asked for
  uint32_t tlsOomFreeHeap = 0;     // free heap at that instant, read where it still meant something
  uint32_t freeHeap = 0;           // free bytes once the transfer gave up, after the session was freed
  uint32_t largestBlock = 0;

  bool ranOutOfMemory() const { return tlsOoms > 0 || allocations.failures > 0; }
};

// Reads the probes. Call INSIDE the framebuffer loan, the instant the transfer
// returns: once the loan ends and a retry runs, these numbers describe a
// different moment than the one that failed.
TransferFailure captureFailure(const HttpDownloader::DownloadError error, const int httpStatus,
                               const bool framebufferLent) {
  TransferFailure failure;
  failure.error = error;
  failure.httpStatus = httpStatus;
  failure.framebufferLent = framebufferLent;
  failure.allocations = heap_probe::read();
  failure.loanFallbacks = tls_scratch::heapFallbackCount();
  failure.loanFallbackBytes = tls_scratch::heapFallbackBytes();
  failure.loanLowWater = static_cast<uint32_t>(tls_scratch::poolLowWaterBytes());
  failure.tlsOoms = tls_scratch::oomCount();
  failure.tlsOomSize = tls_scratch::oomSize();
  failure.tlsOomFreeHeap = tls_scratch::oomFreeHeap();
  failure.freeHeap = ESP.getFreeHeap();
  failure.largestBlock = ESP.getMaxAllocHeap();
  return failure;
}

StrId transferErrorText(const TransferFailure& failure) {
  // An allocation failed during the transfer, so whatever else went wrong, the
  // reader is not being told to check their router.
  if (failure.ranOutOfMemory()) return StrId::STR_FONT_ERR_MEMORY;
  switch (failure.error) {
    case HttpDownloader::NO_CONNECTION:
      return StrId::STR_FONT_ERR_NO_SERVER;
    case HttpDownloader::SERVER_ERROR:
      return StrId::STR_FONT_ERR_SERVER_REFUSED;
    case HttpDownloader::INCOMPLETE:
      return StrId::STR_FONT_ERR_INCOMPLETE;
    case HttpDownloader::FILE_ERROR:
      return StrId::STR_FONT_ERR_SD;
    default:
      // HTTP_ERROR is what the transport could not classify (a malformed URL,
      // mostly). Nothing reached the far end either way.
      return StrId::STR_FONT_ERR_NO_SERVER;
  }
}

// The numbers that tell those causes apart in a photograph of the screen.
// Deliberately untranslated -- it is a code, not prose. Two lines, because one
// did not fit: the X3 screen of 2026-09-19 cut at "oom 2x5368" and the numbers
// that named the cause were the ones that fell off the right-hand edge.
//
// Line one, the transfer:
//
//   E4 no reply heap 24244/12788
//   |  |              `- free / largest block once the transfer gave up. Higher
//   |  |                 than the "f" below, because the session has been freed
//   |  |                 by then -- which is exactly why both are printed
//   |  `- no status line was ever read ("HTTP nnn" when one was)
//   `- HttpDownloader::DownloadError, 4 = NO_CONNECTION
//
// A trailing "nofb" means the framebuffer was never lent, so wolfSSL had no
// 16 KB record buffer to work with and fails for a reason that has nothing to
// do with the network.
//
// Line two, the memory, printed only when something ran short:
//
//   oom 2x5368 f17820 sp33/23456 lw32
//   |          |      |          `- fewest bytes the lent block ever held. A
//   |          |      |             small number here is the loan running out,
//   |          |      |             which is what sends allocations to "sp"
//   |          |      `- allocations the loan could not serve, and their total.
//   |          |         Zero means wolfSSL never touched the system heap
//   |          `- free system heap at the failing call
//   `- two allocations came back null, the last asking 5368 bytes. That null is
//      what wolfSSL returns as MEMORY_E (-125) -- or, when it fails under
//      certificate processing, as PEER_KEY_ERROR (-342) or MP_EXPTMOD_E (-112)
//
// No second line at all means no allocation failed and nothing spilled, and the
// failure is the link or the far end.
// The heap on its own, for a failure that never reached the network: the
// manifest parser could not get a buffer, so there is no transfer to describe.
std::string heapDetail() {
  char buf[32];
  snprintf(buf, sizeof(buf), "heap %u/%u", static_cast<unsigned>(ESP.getFreeHeap()),
           static_cast<unsigned>(ESP.getMaxAllocHeap()));
  return buf;
}

std::string transferErrorDetail(const TransferFailure& failure) {
  char buf[64];
  char reply[16] = "no reply";
  if (failure.httpStatus > 0) snprintf(reply, sizeof(reply), "HTTP %d", failure.httpStatus);
  snprintf(buf, sizeof(buf), "E%d %s heap %u/%u%s", static_cast<int>(failure.error), reply,
           static_cast<unsigned>(failure.freeHeap), static_cast<unsigned>(failure.largestBlock),
           failure.framebufferLent ? "" : " nofb");
  return buf;
}

std::string transferMemoryDetail(const TransferFailure& failure) {
  char oom[32] = "";
  if (failure.tlsOoms > 0) {
    // wolfSSL's own allocator saw it, so the size and the free heap are both
    // from the failing call rather than inferred afterwards.
    snprintf(oom, sizeof(oom), "oom %ux%u f%u", static_cast<unsigned>(failure.tlsOoms),
             static_cast<unsigned>(failure.tlsOomSize), static_cast<unsigned>(failure.tlsOomFreeHeap));
  } else if (failure.allocations.failures > 0) {
    // Something outside wolfSSL ran out -- lwIP, the WiFi driver, this
    // firmware. The hook that counts those cannot read the heap from where it
    // runs, so there is no "f" to print.
    snprintf(oom, sizeof(oom), "oom %ux%u", static_cast<unsigned>(failure.allocations.failures),
             static_cast<unsigned>(failure.allocations.largestSize));
  }
  char spill[32] = "";
  if (failure.loanFallbacks > 0) {
    snprintf(spill, sizeof(spill), "%ssp%u/%u lw%u", oom[0] ? " " : "", static_cast<unsigned>(failure.loanFallbacks),
             static_cast<unsigned>(failure.loanFallbackBytes), static_cast<unsigned>(failure.loanLowWater));
  }
  char buf[72];
  snprintf(buf, sizeof(buf), "%s%s", oom, spill);
  return buf;
}

// The heap gate, and a record of what it saw. Both font transfers ask the same
// question in the same place; the numbers also go to the diagnostics file, so a
// failure that reaches the card does not depend on someone photographing the
// screen. Call INSIDE the framebuffer loan: the floor depends on whether the
// record buffers are coming off the heap (TlsHeapPolicy.h).
bool gateAllowsTls(const char* step, const bool framebufferLent,
                   const tls_heap::Transfer transfer = tls_heap::Transfer::General) {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t largestBlock = ESP.getMaxAllocHeap();
  const uint32_t poolFree = tls_scratch::poolFreeBytes();
  const uint32_t poolBlock = tls_scratch::poolLargestBlock();
  const bool allowed = tls_heap::canStartTls(freeHeap, largestBlock, framebufferLent, poolFree, poolBlock, transfer);
  diag::recordTlsGate(step, freeHeap, largestBlock, framebufferLent, tls_heap::minFree(framebufferLent, transfer),
                      tls_heap::minBlock(framebufferLent), allowed, poolFree, poolBlock);
  LOG_INF("FONT", "Heap at %s: free %u, largest block %u, framebuffer lent %s, floor %u/%u", step,
          static_cast<unsigned>(freeHeap), static_cast<unsigned>(largestBlock), framebufferLent ? "yes" : "no",
          static_cast<unsigned>(tls_heap::minFree(framebufferLent, transfer)),
          static_cast<unsigned>(tls_heap::minBlock(framebufferLent)));
  LOG_INF("FONT", "TLS pool at %s: free %u, largest block %u, floor %u/%u -> %s", step, static_cast<unsigned>(poolFree),
          static_cast<unsigned>(poolBlock), static_cast<unsigned>(tls_heap::MIN_POOL_FREE),
          static_cast<unsigned>(tls_heap::MIN_POOL_BLOCK), allowed ? "allowed" : "REFUSED");
  if (allowed) tls_scratch::monitorSystemHeap();
  if (!allowed) LOG_ERR("FONT", "Not enough TLS memory at %s", step);
  return allowed;
}

}  // namespace

FontDownloadActivity::FontDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiStatusActivity("FontDownload", renderer, mappedInput), fontInstaller_(sdFontSystem.registry()) {}

// --- Lifecycle ---

void FontDownloadActivity::onEnter() {
  UiStatusActivity::onEnter();

  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void FontDownloadActivity::onExit() {
  Activity::onExit();

  // The manifest was kept on the card so a download could re-read one family's
  // file names without holding all of them in RAM. Nothing needs it now.
  Storage.remove(MANIFEST_TMP);

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void FontDownloadActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    finish();
    return;
  }

  {
    RenderLock lock(*this);
    state_ = LOADING_MANIFEST;
  }
  requestUpdateAndWait();

  if (!fetchAndParseManifest()) {
    // Back pressed while waiting for the link is an answer, not a failure to
    // report: leave the screen rather than accuse the server of anything.
    if (cancelRequested_) {
      finish();
      return;
    }
    {
      RenderLock lock(*this);
      state_ = ERROR;
    }
    return;
  }

  {
    RenderLock lock(*this);
    state_ = FAMILY_LIST;
  }
  refreshRows();
  setListSelection(0);
}

// --- Manifest fetching ---

bool FontDownloadActivity::fetchAndParseManifest() {
  // Download manifest to a temp file on SD card to avoid holding both
  // TLS buffers and the full JSON string in RAM simultaneously.

  // The font list is the first thing a reader hits after joining WiFi, and a
  // handshake that fails while the connection settles used to end the trip here.
  auto result = HttpDownloader::OK;
  bool noWifi = false;
  TransferFailure failure;
  for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
    if (!waitForWifi()) {
      result = HttpDownloader::NO_CONNECTION;
      failure.error = result;
      noWifi = true;
      break;
    }

    // This fetch was the last one in the font flow still running on the heap
    // alone. The manifest is ~37 KB over TLS and GitHub redirects it to an
    // asset host that ignores the 2 KB max_fragment_length the reader asks
    // for, so wolfSSL sizes its receive buffer to a 16 KB record: a 16640-byte
    // contiguous allocation, which with WiFi up on a C3 the heap is a few
    // kilobytes short of (TlsScratchHeap.h). The per-file transfer below
    // already lends the framebuffer for exactly this, and
    // OtaUpdateActivity::runUpdateCheck() lends it for the release JSON after
    // the same gap refused every update on the X3. Nothing draws while the
    // bytes are lent: the panel holds the "loading font list" screen.
    {
      GfxRenderer::FrameBufferLoan loan(renderer);
      const tls_scratch::Session tlsScratch;
      const bool framebufferLent = tlsScratch.active();
      if (!framebufferLent) {
        LOG_ERR("FONT", "Framebuffer not lent; the manifest fetch runs on the heap alone");
      }
      // Gated after the loan, so the floor matches where the record buffers
      // will come from (TlsHeapPolicy.h). A handshake started below it does not
      // fail cleanly: wolfSSL spent 60 seconds inside its retry with 1004 bytes
      // free and the reader was unresponsive until the watchdog reset it.
      if (!gateAllowsTls("font list", framebufferLent)) {
        const TransferFailure gated = captureFailure(HttpDownloader::HTTP_ERROR, 0, framebufferLent);
        setError(StrId::STR_FONT_LIST_FAILED, tr(STR_FONT_ERR_MEMORY), transferErrorDetail(gated),
                 transferMemoryDetail(gated));
        Storage.remove(MANIFEST_TMP);
        return false;
      }
      // Armed inside the loan so it counts only what this handshake and this
      // body could not allocate, and read below before the loan ends.
      heap_probe::arm();
      int httpStatus = 0;
      result = HttpDownloader::downloadToFile(FONT_MANIFEST_URL, MANIFEST_TMP, nullptr, nullptr, "", "",
                                              /*allowResume=*/false, nullptr, HttpDownloader::DEFAULT_TIMEOUT_MS,
                                              &httpStatus);
      failure = captureFailure(result, httpStatus, framebufferLent);
    }
    // The loan hands the framebuffer back white, so the next paint has to be a
    // full one rather than a difference against a screen that is no longer there.
    lastDisplayedState_ = WIFI_SELECTION;

    if (result == HttpDownloader::OK) break;
    LOG_ERR("FONT", "Manifest fetch attempt %d of %d failed (%d)", attempt, MAX_ATTEMPTS, result);
    if (attempt < MAX_ATTEMPTS) waitBeforeRetry(RETRY_DELAY_MS * static_cast<uint32_t>(attempt));
  }
  if (result != HttpDownloader::OK) {
    LOG_ERR("FONT",
            "Failed to fetch manifest from %s (error %d, status %d, %u wolfSSL OOM asking %u with %u free, "
            "%u spilled allocations totalling %u bytes, loan low-water %u)",
            FONT_MANIFEST_URL, static_cast<int>(result), failure.httpStatus, static_cast<unsigned>(failure.tlsOoms),
            static_cast<unsigned>(failure.tlsOomSize), static_cast<unsigned>(failure.tlsOomFreeHeap),
            static_cast<unsigned>(failure.loanFallbacks), static_cast<unsigned>(failure.loanFallbackBytes),
            static_cast<unsigned>(failure.loanLowWater));
    setError(StrId::STR_FONT_LIST_FAILED, trId(noWifi ? StrId::STR_FONT_ERR_NO_WIFI : transferErrorText(failure)),
             transferErrorDetail(failure), transferMemoryDetail(failure));
    Storage.remove(MANIFEST_TMP);
    return false;
  }

  // HTTP client is now closed — TLS buffers freed. The manifest is ~37 KB and a DOM
  // of it needs upwards of 60 KB contiguous, which the heap cannot spare with WiFi up:
  // that allocation is what threw bad_alloc and aborted the firmware. Stream it instead.
  //
  // The file stays on the card afterwards. Holding all 26 families' file names costs
  // 27456 bytes, and a font download then has to open a TLS session on what is left:
  // that failed with 4844 bytes free. So the names are re-read for one family at a
  // time, straight from this file, and onExit removes it.
  if (!parseManifest(FontManifestParser::FileRetention::None, nullptr, families_)) return false;

  fontInstaller_.refreshRegistry();

  LOG_DBG("FONT", "Manifest loaded: %zu families", families_.size());
  return true;
}

// Runs as each family finishes parsing, while its file names are still in hand.
// Whether the family is installed, and whether the copy on the card is stale, can
// only be answered from those names, and a moment later they are gone.
void FontDownloadActivity::stampDiskState(void* context, ManifestFamily& family, const ManifestFile* files,
                                          const size_t count) {
  auto* self = static_cast<FontDownloadActivity*>(context);
  family.installed = self->fontInstaller_.isFamilyInstalled(family.name);
  family.hasUpdate = false;
  if (!family.installed) return;

  // Detect updates by comparing manifest file sizes with files on disk. Not a
  // checksum, but a size mismatch reliably indicates a rebuild in practice.
  for (size_t i = 0; i < count; i++) {
    const ManifestFile& file = files[i];
    char path[128];
    FontInstaller::buildFontPath(family.name, file.name, path, sizeof(path));
    // A family can have other sizes installed. Absence here is an update,
    // not a failed font load; report it once on the initial list, not on reloads.
    if (!Storage.exists(path)) {
      if (self->state_ == LOADING_MANIFEST) LOG_DBG("FONT", "Update available: catalog file not installed: %s", path);
      family.hasUpdate = true;
      return;
    }
    HalFile f;
    if (Storage.openFileForRead("FONT", path, f)) {
      const size_t actual = f.fileSize();
      f.close();
      if (actual != file.size) {
        family.hasUpdate = true;
        return;
      }
    } else {
      // Present but unreadable (or removed since exists): HAL logged the read error.
      family.hasUpdate = true;
      return;
    }
  }
}

// Streams the manifest already on the card. `retention` decides whose file names
// survive the parse; `retainFor` names that family when retention is One.
bool FontDownloadActivity::parseManifest(const FontManifestParser::FileRetention retention, const char* retainFor,
                                         std::vector<ManifestFamily>& out) {
  HalFile manifestFile;
  if (!Storage.openFileForRead("FONT", MANIFEST_TMP, manifestFile)) {
    LOG_ERR("FONT", "Failed to open temp manifest");
    setError(StrId::STR_FONT_LIST_FAILED, tr(STR_FONT_ERR_SD), "");
    return false;
  }

  char manifestBytes[32];
  snprintf(manifestBytes, sizeof(manifestBytes), "%u bytes", static_cast<unsigned>(manifestFile.fileSize()));

  out.clear();
  out.shrink_to_fit();

  // On the heap, not the stack: the parser carries a fixed file scratch array and
  // an activity task's stack is not the place for it.
  auto parserOwner = makeUniqueNoThrow<FontManifestParser>();
  if (!parserOwner) {
    manifestFile.close();
    LOG_ERR("FONT", "No room for the manifest parser");
    setError(StrId::STR_FONT_LIST_FAILED, tr(STR_FONT_ERR_MEMORY), heapDetail());
    return false;
  }
  FontManifestParser& parser = *parserOwner;
  parser.retainFiles(retention);
  if (retention == FontManifestParser::FileRetention::One && retainFor) parser.retainFilesFor(retainFor);
  // Only the first pass cares about the card: a re-read for one family is answering
  // "which files", not "what is installed", and that is already known by then.
  if (retention == FontManifestParser::FileRetention::None) parser.setFamilyHook(&stampDiskState, this);

  {
    auto buffer = makeUniqueNoThrow<char[]>(MANIFEST_CHUNK);
    if (!buffer) {
      manifestFile.close();
      LOG_ERR("FONT", "No room for the manifest read buffer");
      setError(StrId::STR_FONT_LIST_FAILED, tr(STR_FONT_ERR_MEMORY), heapDetail());
      return false;
    }
    while (true) {
      const int got = manifestFile.read(buffer.get(), MANIFEST_CHUNK);
      if (got <= 0) break;
      parser.feed(buffer.get(), static_cast<size_t>(got));
      if (parser.hasError()) break;
    }
  }
  manifestFile.close();
  parser.finish();

  LOG_DBG("FONT", "Manifest parsed: free %d bytes, largest block %d bytes", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  if (parser.hasError()) {
    if (parser.outOfMemory()) {
      LOG_ERR("FONT", "Out of memory while reading the font manifest (free %d bytes, largest block %d bytes)",
              ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      setError(StrId::STR_FONT_LIST_FAILED, tr(STR_FONT_ERR_MEMORY), heapDetail());
    } else if (parser.tooLarge()) {
      LOG_ERR("FONT", "Manifest exceeds the %u family / %u file limits",
              static_cast<unsigned>(FontManifestParser::MAX_FAMILIES),
              static_cast<unsigned>(FontManifestParser::MAX_FILES_PER_FAMILY));
      setError(StrId::STR_FONT_LIST_FAILED, tr(STR_FONT_ERR_LIST_TOO_LARGE), "");
    } else {
      LOG_ERR("FONT", "Manifest parse error after %s", manifestBytes);
      setError(StrId::STR_FONT_LIST_FAILED, tr(STR_FONT_ERR_BAD_LIST), manifestBytes);
    }
    return false;
  }

  if (parser.version() != FONTS_MANIFEST_VERSION) {
    LOG_ERR("FONT", "Unsupported manifest version: %d", parser.version());
    setError(StrId::STR_FONT_LIST_FAILED, tr(STR_FONT_ERR_OLD_FIRMWARE), "");
    return false;
  }

  baseUrl_ = parser.baseUrl();
  out = std::move(parser.families());
  return true;
}

// --- Download ---

void FontDownloadActivity::downloadAll() {
  cancelRequested_ = false;
  runBatch([](const ManifestFamily& family) { return !family.installed; });
}

void FontDownloadActivity::updateAll() {
  cancelRequested_ = false;
  runBatch([](const ManifestFamily& family) { return family.hasUpdate; });
}

void FontDownloadActivity::runBatch(const std::function<bool(const ManifestFamily&)>& wanted) {
  // One family that cannot be fetched no longer abandons the rest: the others are
  // independent downloads, and stopping at the first failure meant a single flaky
  // file cost the reader every font behind it in the list.
  // Names, not references: downloadFamily() releases families_ while a transfer
  // is running and rebuilds it afterwards, so any pointer into it goes stale.
  std::vector<std::string> queued;
  for (const auto& family : families_) {
    if (wanted(family)) queued.emplace_back(family.name);
  }

  std::vector<std::string> failed;
  for (const auto& name : queued) {
    if (!downloadFamily(name)) {
      if (cancelRequested_) return;
      failed.push_back(name);
    }
    if (cancelRequested_) return;
  }

  RenderLock lock(*this);
  if (failed.empty()) {
    state_ = COMPLETE;
    return;
  }
  state_ = ERROR;
  std::string names = failed.front();
  for (size_t i = 1; i < failed.size(); i++) names += ", " + failed[i];
  setError(StrId::STR_FONT_INSTALL_FAILED, std::string(tr(STR_FONT_ERR_INSTALL)) + ": " + names, "");
}

bool FontDownloadActivity::showDownloadAllRow() const {
  for (const auto& f : families_) {
    if (!f.installed) return true;
  }
  return false;
}

bool FontDownloadActivity::showUpdateAllRow() const {
  for (const auto& f : families_) {
    if (f.hasUpdate) return true;
  }
  return false;
}

int FontDownloadActivity::specialRowCount() const {
  return (showDownloadAllRow() ? 1 : 0) + (showUpdateAllRow() ? 1 : 0);
}

bool FontDownloadActivity::isDownloadAllRow(int index) const { return showDownloadAllRow() && index == 0; }

bool FontDownloadActivity::isUpdateAllRow(int index) const {
  return showUpdateAllRow() && index == (showDownloadAllRow() ? 1 : 0);
}

int FontDownloadActivity::listItemCount() const {
  return families_.empty() ? 0 : static_cast<int>(families_.size()) + specialRowCount();
}

size_t FontDownloadActivity::totalDownloadSize() const {
  size_t total = 0;
  for (const auto& f : families_) {
    if (!f.installed) total += f.totalSize;
  }
  return total;
}

size_t FontDownloadActivity::totalUpdateSize() const {
  size_t total = 0;
  for (const auto& f : families_) {
    if (f.hasUpdate) total += f.totalSize;
  }
  return total;
}

// Standard CRC32 matching zlib/Python zlib.crc32().
size_t FontDownloadActivity::stagedSize(const char* path) {
  HalFile f;
  if (!Storage.openFileForRead("FONT", path, f)) return 0;
  return f.fileSize();
}

bool FontDownloadActivity::computeFileCrc32(const char* path, uint32_t& outCrc) {
  HalFile f;
  if (!Storage.openFileForRead("FONT", path, f)) {
    return false;
  }
  constexpr size_t BUF_SIZE = 128;
  uint8_t buf[BUF_SIZE];
  uint32_t crc = 0;
  while (f.available()) {
    const int n = f.read(buf, BUF_SIZE);
    if (n <= 0) break;
    crc = esp_rom_crc32_le(crc, buf, static_cast<uint32_t>(n));
  }
  outCrc = crc;
  return true;
}

bool FontDownloadActivity::waitBeforeRetry(const uint32_t ms) {
  const uint32_t until = millis() + ms;
  while (static_cast<int32_t>(until - millis()) > 0) {
    mappedInput.update();
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      cancelRequested_ = true;
      return false;
    }
    delay(20);
  }
  return true;
}

bool FontDownloadActivity::waitForWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  LOG_DBG("FONT", "Waiting for WiFi to come back");
  WiFi.reconnect();
  const uint32_t until = millis() + WIFI_WAIT_MS;
  uint32_t nextReconnect = millis() + RECONNECT_EVERY_MS;
  while (static_cast<int32_t>(until - millis()) > 0) {
    if (WiFi.status() == WL_CONNECTED) return true;
    // One reconnect() at the top is not enough: the first can be issued while
    // the radio is still tearing the old association down, and is then dropped.
    if (static_cast<int32_t>(nextReconnect - millis()) <= 0) {
      WiFi.reconnect();
      nextReconnect = millis() + RECONNECT_EVERY_MS;
    }
    mappedInput.update();
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      cancelRequested_ = true;
      return false;
    }
    delay(100);
  }
  LOG_ERR("FONT", "WiFi did not come back within %u ms", WIFI_WAIT_MS);
  return false;
}

bool FontDownloadActivity::fileAlreadyInstalled(const ManifestFile& file, const char* destPath) {
  if (!Storage.exists(destPath)) return false;
  HalFile f;
  if (!Storage.openFileForRead("FONT", destPath, f)) return false;
  const size_t actual = f.fileSize();
  f.close();
  if (actual != file.size) return false;
  uint32_t crc = 0;
  if (!computeFileCrc32(destPath, crc)) return false;
  return crc == file.crc32;
}

bool FontDownloadActivity::downloadFileWithRetries(const ManifestFile& file, const char* destPath) {
  // A file that survived an earlier run is not fetched again, so retrying a
  // batch that died halfway picks up where it stopped instead of paying for
  // every megabyte a second time.
  if (fileAlreadyInstalled(file, destPath)) {
    LOG_DBG("FONT", "Already installed, skipping %s", file.name);
    return true;
  }

  // Everything lands in a staging file and only takes the real name once it has
  // passed its checksum. Two things follow: the copy already on the card
  // survives a failed update untouched, and a partial can never be discovered as
  // a font, because the registry only accepts names ending in ".cpfont".
  char partPath[192];
  snprintf(partPath, sizeof(partPath), "%s.part", destPath);
  // Bytes left by an older run belong to an older release of this file, so they
  // are not the head of the body about to arrive: only partials this loop
  // creates are ever resumed.
  Storage.remove(partPath);

  // An attempt that moved the partial forward is not a wasted attempt. A body
  // that dies part-way is resumed from the bytes already staged, so five failures
  // that each carry 15 KB are progress, while five that carry nothing are not.
  // The budget below therefore counts only the fruitless ones, and MAX_TOTAL_ATTEMPTS
  // bounds how long a file that is crawling is allowed to keep crawling.
  size_t stagedBefore = 0;
  int fruitless = 0;
  for (int total = 1, attempt = 1; fruitless < MAX_ATTEMPTS && total <= MAX_TOTAL_ATTEMPTS; total++, attempt++) {
    {
      RenderLock lock(*this);
      retryAttempt_ = attempt - 1;
      fileProgress_ = 0;
      fileTotal_ = file.size;
      lastDrawnProgressStep_ = -1;
      refreshProgressLines();
    }
    requestUpdateAndWait();

    // A link that has not come back yet costs an attempt rather than the file:
    // the bytes already staged stay, and the next attempt resumes from them.
    if (!waitForWifi()) {
      if (cancelRequested_) {
        Storage.remove(partPath);
        return false;
      }
      LOG_ERR("FONT", "No WiFi for attempt %d for %s", total, file.name);
      setError(StrId::STR_FONT_INSTALL_FAILED, tr(STR_FONT_ERR_LOST_WIFI), "");
      fruitless++;
      if (fruitless < MAX_ATTEMPTS && total < MAX_TOTAL_ATTEMPTS &&
          !waitBeforeRetry(RETRY_DELAY_MS * static_cast<uint32_t>(attempt))) {
        Storage.remove(partPath);
        return false;
      }
      continue;
    }

    LOG_DBG("FONT", "Fetching %s: free %d bytes, largest block %d bytes", file.name, ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());

    const std::string url = baseUrl_ + file.name;
    // The framebuffer's 48 KB go to wolfSSL for the length of the transfer, which
    // is the only place the reader has the room for a 16 KB TLS record buffer: with
    // WiFi up the heap runs out a few kilobytes short, every time, at any level of
    // fragmentation. Nothing may draw while the bytes are lent, so the progress bar
    // holds still until the file lands and the panel keeps the screen drawn above.
    drawingSuspended_ = true;
    HttpDownloader::DownloadError result;
    TransferFailure failure;
    {
      GfxRenderer::FrameBufferLoan loan(renderer);
      const tls_scratch::Session tlsScratch;
      const bool framebufferLent = tlsScratch.active();
      if (!framebufferLent) {
        LOG_ERR("FONT", "Framebuffer not lent; the transfer runs on the heap alone");
      }
      // Gated after the loan, so the floor matches where the record buffers
      // will come from (TlsHeapPolicy.h). A handshake started below it does
      // not fail cleanly: wolfSSL spent 60 seconds inside its retry with 1004
      // bytes free and the reader was unresponsive until the watchdog reset it.
      if (!gateAllowsTls("font file", framebufferLent, tls_heap::Transfer::FontFile)) {
        const TransferFailure gated = captureFailure(HttpDownloader::HTTP_ERROR, 0, framebufferLent);
        setError(StrId::STR_FONT_INSTALL_FAILED, tr(STR_FONT_ERR_MEMORY), transferErrorDetail(gated),
                 transferMemoryDetail(gated));
        drawingSuspended_ = false;
        return false;
      }
      // Armed inside the loan so it counts only what this handshake and this
      // body could not allocate, and read below before the loan ends.
      heap_probe::arm();
      int httpStatus = 0;
      result = HttpDownloader::downloadToFile(
          url, partPath,
          [this](size_t downloaded, size_t total) {
            fileProgress_ = downloaded;
            fileTotal_ = total;
            // Cancel is polled on every chunk; only the repaint is rationed.
            mappedInput.update();
            if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
                mappedInput.wasPressed(MappedInputManager::Button::Back)) {
              cancelRequested_ = true;
            }
            if (drawingSuspended_) return;
            const int percent = total > 0 ? static_cast<int>(downloaded * 100 / total) : 0;
            const int step = percent / PROGRESS_STEP_PERCENT;
            if (step == lastDrawnProgressStep_) return;
            lastDrawnProgressStep_ = step;
            requestUpdate(true);
          },
          &cancelRequested_, "", "", /*allowResume=*/true, nullptr, HttpDownloader::DEFAULT_TIMEOUT_MS, &httpStatus);
      failure = captureFailure(result, httpStatus, framebufferLent);
    }
    drawingSuspended_ = false;
    // The loan hands the framebuffer back white, so the next paint has to be a
    // full one rather than a difference against a screen that is no longer there.
    lastDisplayedState_ = WIFI_SELECTION;
    requestUpdateAndWait();

    if (result == HttpDownloader::ABORTED || cancelRequested_) {
      // A cancel is an answer, not an interruption to be picked up later: leave
      // no staging file behind for the next visit to puzzle over.
      Storage.remove(partPath);
      cancelRequested_ = true;
      return false;
    }

    if (result != HttpDownloader::OK) {
      LOG_ERR("FONT",
              "Download attempt %d failed for %s (%d, status %d, %u wolfSSL OOM asking %u with %u free, "
              "%u spilled allocations totalling %u bytes, loan low-water %u)",
              total, file.name, result, failure.httpStatus, static_cast<unsigned>(failure.tlsOoms),
              static_cast<unsigned>(failure.tlsOomSize), static_cast<unsigned>(failure.tlsOomFreeHeap),
              static_cast<unsigned>(failure.loanFallbacks), static_cast<unsigned>(failure.loanFallbackBytes),
              static_cast<unsigned>(failure.loanLowWater));
      setError(StrId::STR_FONT_INSTALL_FAILED, std::string(trId(transferErrorText(failure))) + ": " + file.name,
               transferErrorDetail(failure), transferMemoryDetail(failure));
    } else {
      uint32_t actualCrc = 0;
      if (!computeFileCrc32(partPath, actualCrc)) {
        LOG_ERR("FONT", "Failed to open file for CRC check: %s", partPath);
        setError(StrId::STR_FONT_INSTALL_FAILED, std::string(tr(STR_FONT_ERR_CHECKSUM_READ)) + ": " + file.name, "");
      } else if (actualCrc != file.crc32) {
        // A body that arrived corrupted is worth fetching again: the manifest
        // checksum is the only thing that separates a bad transfer from a font
        // the renderer would later choke on.
        LOG_ERR("FONT", "CRC32 mismatch for %s: got %08x expected %08x", file.name, actualCrc, file.crc32);
        setError(StrId::STR_FONT_INSTALL_FAILED, std::string(tr(STR_FONT_ERR_CHECKSUM)) + ": " + file.name, "");
      } else if (!fontInstaller_.validateCpfontFile(partPath)) {
        LOG_ERR("FONT", "Invalid .cpfont: %s", partPath);
        setError(StrId::STR_FONT_INSTALL_FAILED, std::string(tr(STR_FONT_ERR_BAD_FILE)) + ": " + file.name, "");
      } else if (!promoteStagedFile(partPath, destPath)) {
        setError(StrId::STR_FONT_INSTALL_FAILED, std::string(tr(STR_FONT_ERR_INSTALL)) + ": " + file.name, "");
      } else {
        LOG_DBG("FONT", "Downloaded %s (size=%u crc32=%08x)", file.name, static_cast<unsigned>(file.size), actualCrc);
        RenderLock lock(*this);
        retryAttempt_ = 0;
        return true;
      }
    }

    // A transfer that stopped early leaves bytes the next attempt resumes from,
    // so only a body that arrived whole and wrong is swept away here.
    if (result == HttpDownloader::OK) Storage.remove(partPath);

    const size_t stagedNow = stagedSize(partPath);
    if (stagedNow > stagedBefore) {
      LOG_DBG("FONT", "Attempt %d carried %u bytes, %u staged of %u", total,
              static_cast<unsigned>(stagedNow - stagedBefore), static_cast<unsigned>(stagedNow),
              static_cast<unsigned>(file.size));
      fruitless = 0;
      attempt = 0;
    } else {
      fruitless++;
    }
    stagedBefore = stagedNow;

    if (fruitless < MAX_ATTEMPTS && total < MAX_TOTAL_ATTEMPTS &&
        !waitBeforeRetry(RETRY_DELAY_MS * static_cast<uint32_t>(attempt + 1))) {
      Storage.remove(partPath);
      return false;
    }
  }

  Storage.remove(partPath);
  LOG_ERR("FONT", "Giving up on %s after %d fruitless attempts", file.name, MAX_ATTEMPTS);
  RenderLock lock(*this);
  retryAttempt_ = 0;
  return false;
}

bool FontDownloadActivity::promoteStagedFile(const char* partPath, const char* destPath) {
  // The old copy goes only now, with a verified replacement in hand.
  if (Storage.exists(destPath) && !Storage.remove(destPath)) {
    LOG_ERR("FONT", "Failed to remove the previous file: %s", destPath);
    return false;
  }
  if (!Storage.rename(partPath, destPath)) {
    LOG_ERR("FONT", "Failed to rename %s to %s", partPath, destPath);
    return false;
  }
  return true;
}

bool FontDownloadActivity::downloadFamily(const std::string& familyName) {
  // A failed update must not cost the reader the copy already on the card, so
  // only a family that was not installed before this download is cleared out.
  bool wasInstalled = false;
  int familyIndex = -1;
  for (size_t i = 0; i < families_.size(); i++) {
    if (familyName == families_[i].name) {
      wasInstalled = families_[i].installed;
      familyIndex = static_cast<int>(i);
      break;
    }
  }
  if (familyIndex < 0) {
    LOG_ERR("FONT", "No family named %s in the manifest", familyName.c_str());
    setError(StrId::STR_FONT_LIST_FAILED, tr(STR_FONT_ERR_BAD_LIST), "");
    return false;
  }

  {
    RenderLock lock(*this);
    state_ = DOWNLOADING;
    downloadingFamilyIndex_ = familyIndex;
    downloadingFamilyName_ = familyName;
    fileProgress_ = 0;
    fileTotal_ = 0;
    retryAttempt_ = 0;
    refreshProgressLines();
  }
  requestUpdateAndWait();

  if (!fontInstaller_.ensureFamilyDir(familyName.c_str())) {
    setError(StrId::STR_FONT_INSTALL_FAILED, tr(STR_FONT_ERR_MKDIR), "");
    return false;
  }

  // The file names for this one family, read back from the manifest still on the
  // card. Everything else keeps only its counts, which is what leaves room for the
  // TLS session each file needs.
  std::vector<ManifestFamily> reread;
  if (!parseManifest(FontManifestParser::FileRetention::One, familyName.c_str(), reread)) return false;
  std::vector<ManifestFile> files;
  for (auto& candidate : reread) {
    if (familyName == candidate.name) {
      files = std::move(candidate.files);
      break;
    }
  }
  reread.clear();
  reread.shrink_to_fit();
  if (files.empty()) {
    LOG_ERR("FONT", "No files listed for %s in the manifest", familyName.c_str());
    setError(StrId::STR_FONT_LIST_FAILED, tr(STR_FONT_ERR_BAD_LIST), "");
    return false;
  }

  // The list of families is not needed again until the download screen is gone,
  // and the transfer needs every byte it was holding. GitHub's asset host ignores
  // the 2 KB max_fragment_length the reader asks for, so wolfSSL sizes its buffers
  // to 16 KB records mid-body: a transfer that began with 39812 bytes free died at
  // 32768 bytes with 12480 free, while the manifest's own transfer succeeded from
  // 48384. Releasing the list here hands that difference back. reloadFamilies()
  // rebuilds it from the copy still on the card once the files are in.
  std::vector<ManifestFamily>().swap(families_);
  // The rows point into the list that was just released, and the failure paths
  // below can reach the list screen again without rebuilding them.
  // clear() destroys strings but keeps the three backing arrays. On C3 these
  // otherwise pin 33 * (sizeof(ListItem) + 2 * sizeof(string)) = 3300 bytes
  // through every transfer. The progress view never uses them; refreshRows()
  // rebuilds them after reloadFamilies(), including cancel/error paths.
  const size_t rowStorage = rows_.capacity() * sizeof(freeink::ui::ListItem) +
                            (rowLabels_.capacity() + rowValues_.capacity()) * sizeof(std::string);
  std::vector<freeink::ui::ListItem>().swap(rows_);
  std::vector<std::string>().swap(rowLabels_);
  std::vector<std::string>().swap(rowValues_);
  LOG_DBG("FONT", "Released list storage: %u payload bytes", static_cast<unsigned>(rowStorage));

  bool ok = true;
  for (const auto& file : files) {
    char destPath[128];
    FontInstaller::buildFontPath(familyName.c_str(), file.name, destPath, sizeof(destPath));

    if (!downloadFileWithRetries(file, destPath)) {
      ok = false;
      break;
    }
    currentFileIndex_++;
    refreshProgressLines();
  }

  fontInstaller_.refreshRegistry();
  if (!ok) {
    // The files that did land stay: a reader on a poor link gets the family a
    // few styles at a time across runs, and the size check in the manifest
    // marks what is still missing as an update. Only a family that arrived
    // with nothing at all is cleared, so no empty directory is left behind.
    if (!wasInstalled && !fontInstaller_.isFamilyInstalled(familyName.c_str())) {
      // Discovery already excluded this empty family. Removing its directory
      // cannot change the registry; do not scan every installed family again.
      // rmdir also refuses a non-empty directory, preserving unrelated files.
      char dirPath[128];
      FontInstaller::buildFontPath(familyName.c_str(), "", dirPath, sizeof(dirPath));
      if (!Storage.rmdir(dirPath)) LOG_DBG("FONT", "Kept non-empty or unreadable directory: %s", dirPath);
    }
  }

  // Rebuilt from the card, so installed and hasUpdate come back stamped from what
  // is actually there now rather than from what this function believes it wrote.
  if (!reloadFamilies()) {
    refreshRows();
    return false;
  }
  refreshRows();

  if (!ok && cancelRequested_) {
    RenderLock lock(*this);
    state_ = FAMILY_LIST;
  }
  return ok;
}

bool FontDownloadActivity::reloadFamilies() {
  if (!families_.empty()) return true;
  return parseManifest(FontManifestParser::FileRetention::None, nullptr, families_);
}

void FontDownloadActivity::downloadSingleFamily(const std::string& familyName) {
  cancelRequested_ = false;
  if (downloadFamily(familyName)) {
    RenderLock lock(*this);
    state_ = COMPLETE;
    return;
  }
  if (cancelRequested_) return;
  RenderLock lock(*this);
  state_ = ERROR;
}

void FontDownloadActivity::promptDeleteSelectedFamily() {
  const int pendingDeleteFamilyIndex = familyIndexFromList(listSelection());
  if (pendingDeleteFamilyIndex < 0 || pendingDeleteFamilyIndex >= static_cast<int>(families_.size())) {
    return;
  }

  std::string heading = tr(STR_DELETE);
  const auto& family = families_[pendingDeleteFamilyIndex];
  std::string body = family.name;
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, body),
                         [this](const ActivityResult& result) { onDeleteConfirmationResult(result); });
}

void FontDownloadActivity::onDeleteConfirmationResult(const ActivityResult& result) {
  if (result.isCancelled) {
    requestUpdate();
    return;
  }

  auto& family = families_[familyIndexFromList(listSelection())];

  if (fontInstaller_.deleteFamily(family.name) != FontInstaller::Error::OK) {
    RenderLock lock(*this);
    state_ = ERROR;
    setError(StrId::STR_FONT_INSTALL_FAILED, tr(STR_FONT_DELETE_FAILED), "");
  } else {
    fontInstaller_.refreshRegistry();
    family.installed = false;
    family.hasUpdate = false;
  }
  // The deleted family drops its "Installed" mark, and the Download-all row may
  // appear now that something is missing again.
  refreshRows();

  requestUpdate();
}

bool FontDownloadActivity::isSelectedFamilyDeletable() const {
  if (isDownloadAllRow(listSelection()) || isUpdateAllRow(listSelection())) return false;
  if (listSelection() < specialRowCount() || listSelection() >= listItemCount()) return false;
  const auto& family = families_[familyIndexFromList(listSelection())];
  return family.installed && !family.hasUpdate;
}

// --- Rows and lines ---

std::string FontDownloadActivity::formatSize(size_t bytes) {
  char buf[32];
  if (bytes >= 1024 * 1024) {
    snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else if (bytes >= 1024) {
    snprintf(buf, sizeof(buf), "%.0f KB", static_cast<double>(bytes) / 1024.0);
  } else {
    snprintf(buf, sizeof(buf), "%zu B", bytes);
  }
  return buf;
}

void FontDownloadActivity::refreshRows() {
  const int count = listItemCount();
  rowLabels_.clear();
  rowValues_.clear();
  rows_.clear();
  if (count <= 0) return;

  rowLabels_.reserve(count);
  rowValues_.reserve(count);
  for (int index = 0; index < count; ++index) {
    if (isDownloadAllRow(index)) {
      rowLabels_.push_back(std::string(tr(STR_DOWNLOAD_ALL)) + " (" + formatSize(totalDownloadSize()) + ")");
      rowValues_.emplace_back();
      continue;
    }
    if (isUpdateAllRow(index)) {
      rowLabels_.push_back(std::string(tr(STR_UPDATE_ALL)) + " (" + formatSize(totalUpdateSize()) + ")");
      rowValues_.emplace_back();
      continue;
    }
    const auto& family = families_[familyIndexFromList(index)];
    rowLabels_.push_back(family.name);
    rowValues_.push_back(family.hasUpdate ? tr(STR_UPDATE_AVAILABLE) : family.installed ? tr(STR_INSTALLED) : "");
  }

  // Second pass: the strings must stop moving before their addresses are taken.
  rows_.resize(count);
  for (int index = 0; index < count; ++index) {
    rows_[index] = freeink::ui::ListItem{};
    rows_[index].label = rowLabels_[index].c_str();
    rows_[index].actionValue = static_cast<int16_t>(index);
    if (!rowValues_[index].empty()) rows_[index].value = rowValues_[index].c_str();
    if (isDownloadAllRow(index) || isUpdateAllRow(index)) continue;
    const auto& family = families_[familyIndexFromList(index)];
    if (family.description[0] != '\0') rows_[index].subtitle = family.description;
  }
}

void FontDownloadActivity::refreshProgressLines() {
  statusLine_ = std::string(tr(STR_DOWNLOADING)) + " " + downloadingFamilyName_ + " (" +
                std::to_string(currentFileIndex_ + 1) + "/" + std::to_string(currentFileTotal_) + ")";
  // Above the status line rather than below the bar: a retry is context for
  // what is being downloaded, and it reads as that only when it comes first.
  retryLine_ = retryAttempt_ > 0 ? std::string(tr(STR_RETRY)) + " " + std::to_string(retryAttempt_ + 1) + "/" +
                                       std::to_string(MAX_ATTEMPTS)
                                 : std::string();
}

// Every failure path goes through here so none of them can leave a stale
// headline, message or code behind from the previous one.
void FontDownloadActivity::setError(const StrId headline, std::string message, std::string detail, std::string memory) {
  errorHeadline_ = headline;
  errorMessage_ = std::move(message);
  errorDetail_ = std::move(detail);
  errorMemory_ = std::move(memory);
}

// --- Screen ---

UiStatusActivity::StatusView FontDownloadActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_FONT_BROWSER);
  // A differential pass leaves the previous screen showing through as grey
  // residue, which is why the list used to sit under the progress bar for the
  // whole download. The screen changes wholesale between states and at each new
  // file, so those paints take a cleanup pass; the bar's own steps stay
  // differential.
  const bool screenChanged = state_ != lastDisplayedState_ || downloadingFamilyIndex_ != lastDisplayedFamilyIndex_ ||
                             currentFileIndex_ != lastDisplayedFileIndex_ || retryAttempt_ != lastDisplayedRetry_;
  view.refresh = screenChanged ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;

  switch (state_) {
    case WIFI_SELECTION:
      // The picker owns the screen.
      view.hidden = true;
      break;
    case LOADING_MANIFEST:
      view.lines = {tr(STR_LOADING_FONT_LIST), nullptr, nullptr, nullptr};
      view.backHint = "";
      break;
    case FAMILY_LIST:
      if (rows_.empty()) {
        view.lines = {tr(STR_NO_FONTS_AVAILABLE), nullptr, nullptr, nullptr};
        break;
      }
      view.listItems = rows_.data();
      view.listCount = static_cast<int>(rows_.size());
      view.listHasSubtitle = true;
      view.confirmHint = isSelectedFamilyDeletable()       ? tr(STR_DELETE)
                         : isUpdateAllRow(listSelection()) ? tr(STR_UPDATE)
                                                           : tr(STR_DOWNLOAD);
      break;
    case DOWNLOADING:
      view.lines = {retryLine_.empty() ? statusLine_.c_str() : retryLine_.c_str(),
                    retryLine_.empty() ? nullptr : statusLine_.c_str(), nullptr, nullptr};
      view.showProgress = true;
      view.progressValue = static_cast<int>(fileProgress_);
      view.progressMax = fileTotal_ > 0 ? static_cast<int>(fileTotal_) : 1;
      view.backHint = tr(STR_CANCEL);
      break;
    case COMPLETE:
      view.lines = {tr(STR_FONT_INSTALLED), nullptr, nullptr, nullptr};
      break;
    case ERROR:
      // Headline, then what went wrong, then the numbers on two lines. A reader
      // who cannot read a serial log photographs this screen, and those lines
      // are what has to say which failure it was. Two lines and not one because
      // the panel truncates: everything past about 38 characters is lost.
      view.lines = {trId(errorHeadline_), errorMessage_.empty() ? nullptr : errorMessage_.c_str(),
                    errorDetail_.empty() ? nullptr : errorDetail_.c_str(),
                    errorMemory_.empty() ? nullptr : errorMemory_.c_str()};
      view.confirmHint = tr(STR_RETRY);
      break;
  }
  return view;
}

void FontDownloadActivity::afterRender() {
  lastDisplayedState_ = state_;
  lastDisplayedFamilyIndex_ = downloadingFamilyIndex_;
  lastDisplayedFileIndex_ = currentFileIndex_;
  lastDisplayedRetry_ = retryAttempt_;
}

// --- Input handling ---

void FontDownloadActivity::onListActivated(const int index) {
  if (state_ != FAMILY_LIST || families_.empty()) return;
  setListSelection(index);

  if (isDownloadAllRow(index)) {
    currentFileIndex_ = 0;
    currentFileTotal_ = 0;
    for (const auto& f : families_) {
      if (!f.installed) currentFileTotal_ += f.fileCount;
    }
    downloadAll();
  } else if (isUpdateAllRow(index)) {
    currentFileIndex_ = 0;
    currentFileTotal_ = 0;
    for (const auto& f : families_) {
      if (f.hasUpdate) currentFileTotal_ += f.fileCount;
    }
    updateAll();
  } else {
    const auto& family = families_[familyIndexFromList(index)];
    if (!family.installed || family.hasUpdate) {
      currentFileIndex_ = 0;
      currentFileTotal_ = family.fileCount;
      // Copied before the call: the list it points into is released mid-download.
      downloadSingleFamily(std::string(family.name));
    } else {
      promptDeleteSelectedFamily();
      return;
    }
  }
  requestUpdateAndWait();
}

void FontDownloadActivity::onBackButton() {
  // The result screens go back to the list; the list itself leaves.
  if (state_ == COMPLETE || state_ == ERROR) {
    {
      RenderLock lock(*this);
      state_ = FAMILY_LIST;
    }
    requestUpdate();
    return;
  }
  finish();
}

void FontDownloadActivity::onConfirmButton() {
  if (state_ == COMPLETE) {
    onBackButton();
    return;
  }
  if (state_ != ERROR) return;

  // Retry the family that failed, when it is still one the manifest offers.
  const auto retry = std::find_if(families_.begin(), families_.end(),
                                  [this](const ManifestFamily& f) { return downloadingFamilyName_ == f.name; });
  if (retry == families_.end()) {
    onBackButton();
    return;
  }
  currentFileIndex_ = 0;
  currentFileTotal_ = retry->fileCount;
  downloadSingleFamily(downloadingFamilyName_);
  requestUpdateAndWait();
}

// Nothing on this screen answers while the manifest or a file is in flight: the
// download blocks this task and polls for a cancel itself.
bool FontDownloadActivity::handleCustomInput() {
  return state_ == WIFI_SELECTION || state_ == LOADING_MANIFEST || state_ == DOWNLOADING;
}
