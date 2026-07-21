// Hot translation unit: compiled -O2 instead of the global -Os (speed-plan
// 6.3). The inner loops here dominate layout/render time on the flash-cache-
// starved ESP32-C3; the size cost is confined to this file.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif

#include "SleepWallpaperStage.h"

#include <Esp.h>  // ESP.getFreeHeap / getMaxAllocHeap
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <strings.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "../CrossPointSettings.h"
#include "../activities/boot_sleep/SleepInfoOverlay.h"
#include "PxcPlanePacker.h"
#include "SleepWallpaperIndexStore.h"
#include "Wallpaper.h"

namespace crosspoint {
namespace sleep {
namespace stage {

namespace {

constexpr char kStagePath[] = "/.crosspoint/sleep_stage.bin";
constexpr char kStageTmpPath[] = "/.crosspoint/sleep_stage.tmp";

constexpr uint32_t kMagic = 0x47545353;  // "SSTG"
constexpr uint8_t kVersion = 1;
constexpr size_t kHeaderBytes = 512;
constexpr size_t kMaxPathLen = 400;

// Payload rows converted per pumpIdle call. 48 rows on the X3 is a ~6.3 KB
// sequential SD read plus cheap bit-packing — a few ms, so idle input latency
// is never hurt even mid-conversion.
constexpr int kRowsPerPump = 48;
// Physical framebuffer rows per band; bounds scratch RAM at
// bandRows * strideBytes per plane (X3: ~7.9 KB, three planes ~24 KB).
constexpr int kBandRows = 80;

// Do not BEGIN a prestage conversion below these heap floors. The conversion
// holds ~24 KB of plane scratch for its duration; starting it when memory is
// already tight competes with an active reader chapter build, whose layout
// allocation is a throwing new that aborts the firmware on OOM. Above these
// floors there is comfortable room for both; below, skip prewarm for this idle
// cycle and let wake decode the wallpaper on demand (PxcSleepRenderer streams
// row-by-row). Only gates the START (Phase::Pending) — a conversion already in
// flight runs to completion, and its per-band scratch alloc is itself nothrow.
constexpr uint32_t kMinFreeHeapToStage = 60000;
constexpr uint32_t kMinBlockToStage = 40000;

struct StageHeader {
  uint8_t quality = 0;
  uint8_t format = 0;
  uint8_t complete = 0;
  uint16_t strideBytes = 0;
  uint16_t phyHeight = 0;
  uint16_t pxcWidth = 0;
  uint16_t pxcHeight = 0;
  std::string path;
};

// memcpy-based field packing: RISC-V faults on unaligned multi-byte access,
// so the header buffer is never cast to wider pointer types.
template <typename T>
void putField(uint8_t* buf, size_t& off, const T value) {
  memcpy(buf + off, &value, sizeof(T));
  off += sizeof(T);
}
template <typename T>
void getField(const uint8_t* buf, size_t& off, T& value) {
  memcpy(&value, buf + off, sizeof(T));
  off += sizeof(T);
}

bool writeHeader(HalFile& out, const StageHeader& h) {
  uint8_t buf[kHeaderBytes] = {0};
  size_t off = 0;
  putField(buf, off, kMagic);
  putField(buf, off, kVersion);
  putField(buf, off, h.quality);
  putField(buf, off, h.format);
  putField(buf, off, h.complete);
  putField(buf, off, h.strideBytes);
  putField(buf, off, h.phyHeight);
  putField(buf, off, h.pxcWidth);
  putField(buf, off, h.pxcHeight);
  const uint16_t pathLen = static_cast<uint16_t>(h.path.size() < kMaxPathLen ? h.path.size() : kMaxPathLen);
  putField(buf, off, pathLen);
  memcpy(buf + off, h.path.data(), pathLen);
  if (!out.seek(0)) return false;
  return out.write(buf, kHeaderBytes) == kHeaderBytes;
}

bool readHeader(HalFile& in, StageHeader& h) {
  uint8_t buf[kHeaderBytes];
  if (!in.seek(0) || in.read(buf, kHeaderBytes) != static_cast<int>(kHeaderBytes)) return false;
  size_t off = 0;
  uint32_t magic = 0;
  uint8_t version = 0;
  getField(buf, off, magic);
  getField(buf, off, version);
  if (magic != kMagic || version != kVersion) return false;
  getField(buf, off, h.quality);
  getField(buf, off, h.format);
  getField(buf, off, h.complete);
  getField(buf, off, h.strideBytes);
  getField(buf, off, h.phyHeight);
  getField(buf, off, h.pxcWidth);
  getField(buf, off, h.pxcHeight);
  uint16_t pathLen = 0;
  getField(buf, off, pathLen);
  if (pathLen > kMaxPathLen) return false;
  h.path.assign(reinterpret_cast<const char*>(buf + off), pathLen);
  return true;
}

bool sleepScreenCanRouteToCustom() {
  switch (SETTINGS.sleepScreen) {
    case CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM:
    case CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM:
    case CrossPointSettings::SLEEP_SCREEN_MODE::RANDOM_LOGO_CUSTOM:
      return true;
    default:
      return false;
  }
}

bool headerMatchesCurrentSetup(const StageHeader& h, const uint16_t strideBytes, const uint16_t phyHeight) {
  return h.quality == SETTINGS.sleepImageQuality && h.format == SETTINGS.wallpaperFormat &&
         h.format == CrossPointSettings::WALLPAPER_PXC && h.strideBytes == strideBytes && h.phyHeight == phyHeight;
}

enum class Phase : uint8_t { Pending, Converting, Done };

Phase s_phase = Phase::Pending;
StageHeader s_header;
size_t s_payloadOffset = 0;
int s_bytesPerRow = 0;
int s_planeCount = 0;
int s_band = 0;
int s_row = 0;
HalFile s_src;
HalFile s_out;
std::unique_ptr<uint8_t[]> s_scratch[3];
std::unique_ptr<uint8_t[]> s_readBuf;
// Held for the whole conversion (windex pattern): a per-tick lock would thrash
// the CPU frequency and fight the render task's own power lock.
std::unique_ptr<HalPowerManager::Lock> s_powerLock;

// Plane rules follow the header captured at pick time, not live SETTINGS —
// a quality change mid-conversion must not mix rules; the stale stage is
// instead rejected by headerMatchesCurrentSetup() at the next validation.
pxc_plane::Plane planeForIndex(const int idx, const uint8_t quality) {
  if (idx == 1) return pxc_plane::Plane::Lsb;
  if (idx == 2) return pxc_plane::Plane::Msb;
  return quality == CrossPointSettings::SLEEP_IMG_PRETTY ? pxc_plane::Plane::BwBase : pxc_plane::Plane::BwFastDithered;
}

// Stage plane data is BAND-MAJOR: band 0's planes back to back, then band
// 1's, and so on. The conversion then only ever APPENDS (no writes past EOF,
// which FAT handles poorly), while the lock-side per-plane read is a handful
// of short seeks.
int rowsInBand(const int band, const uint16_t phyHeight) {
  const int remaining = phyHeight - band * kBandRows;
  return remaining < kBandRows ? remaining : kBandRows;
}

size_t bandDataOffset(const int band, const int planeCount, const uint16_t strideBytes, const uint16_t phyHeight) {
  size_t off = kHeaderBytes;
  for (int b = 0; b < band; b++) {
    off += static_cast<size_t>(planeCount) * rowsInBand(b, phyHeight) * strideBytes;
  }
  return off;
}

void abortConversion(const char* reason) {
  LOG_ERR("SSTG", "Prestage aborted: %s", reason);
  s_src.close();
  s_out.close();
  for (auto& s : s_scratch) s.reset();
  s_readBuf.reset();
  s_powerLock.reset();
  Storage.remove(kStageTmpPath);
  // Done, not Pending: an SD-level failure would likely repeat, so don't
  // retry until the next boot. The lock path falls back to the blocking
  // render and nothing is lost but the head start.
  s_phase = Phase::Done;
}

void finishConversion() {
  s_header.complete = 1;
  if (!writeHeader(s_out, s_header)) {
    abortConversion("header rewrite failed");
    return;
  }
  s_src.close();
  s_out.close();
  for (auto& s : s_scratch) s.reset();
  s_readBuf.reset();
  s_powerLock.reset();
  Storage.remove(kStagePath);
  if (!Storage.rename(kStageTmpPath, kStagePath)) {
    LOG_ERR("SSTG", "Prestage rename failed");
    Storage.remove(kStageTmpPath);
    s_phase = Phase::Done;
    return;
  }
  LOG_INF("SSTG", "Sleep wallpaper prestaged: %s (%d planes)", s_header.path.c_str(), s_planeCount);
  s_phase = Phase::Done;
}

// Validate an existing stage file. Returns true when it is complete and
// matches the current settings/geometry, so no work is needed this session.
bool existingStageUsable(const uint16_t strideBytes, const uint16_t phyHeight) {
  if (!Storage.exists(kStagePath)) return false;
  HalFile f;
  if (!Storage.openFileForRead("SSTG", kStagePath, f)) return false;
  StageHeader h;
  const bool ok = readHeader(f, h) && h.complete == 1 && headerMatchesCurrentSetup(h, strideBytes, phyHeight);
  f.close();
  if (!ok) {
    Storage.remove(kStagePath);
  }
  return ok;
}

bool startConversion(const uint16_t strideBytes, const uint16_t phyHeight) {
  // Pick through the normal rotation machinery so the cursor advance,
  // repeat-guard and lastSleepWallpaperPath bookkeeping all behave exactly
  // as a lock-time pick would. The probe only validates the pxc header.
  StageHeader header;
  header.quality = SETTINGS.sleepImageQuality;
  header.format = SETTINGS.wallpaperFormat;
  header.strideBytes = strideBytes;
  header.phyHeight = phyHeight;

  size_t payloadOffset = 0;
  int bytesPerRow = 0;
  wallpaper::RenderProbe probe = [&](const wallpaper::SleepPick& pick) -> bool {
    const std::string& p = pick.fullPath;
    if (p.size() < 4 || strcasecmp(p.c_str() + p.size() - 4, ".pxc") != 0) return false;
    HalFile f;
    if (!Storage.openFileForRead("SSTG", p, f)) return false;
    uint16_t w = 0, hgt = 0;
    const bool headerOk = f.read(&w, 2) == 2 && f.read(&hgt, 2) == 2;
    const size_t off = f.position();
    f.close();
    // Same panel-size gate as renderPxcSleepScreen (1 px slack), expressed
    // against physical geometry: portrait logical width == phyHeight,
    // logical height == strideBytes * 8.
    const int logicalW = phyHeight;
    const int logicalH = strideBytes * 8;
    if (!headerOk || abs(static_cast<int>(w) - logicalW) > 1 || abs(static_cast<int>(hgt) - logicalH) > 1) {
      return false;
    }
    header.pxcWidth = w;
    header.pxcHeight = hgt;
    header.path = p;
    payloadOffset = off;
    bytesPerRow = (w + 3) / 4;
    return true;
  };

  const auto pick = wallpaper::nextSleepFile(probe);
  if (!pick.hasImage() || header.path.empty()) return false;

  s_planeCount = SETTINGS.sleepImageQuality == CrossPointSettings::SLEEP_IMG_PRETTY ? 3 : 1;

  if (!Storage.openFileForRead("SSTG", header.path.c_str(), s_src)) return false;
  Storage.mkdir("/.crosspoint");
  Storage.remove(kStageTmpPath);
  if (!Storage.openFileForWrite("SSTG", kStageTmpPath, s_out)) {
    s_src.close();
    return false;
  }
  header.complete = 0;
  if (!writeHeader(s_out, header)) {
    s_src.close();
    s_out.close();
    Storage.remove(kStageTmpPath);
    return false;
  }

  s_readBuf = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(kRowsPerPump) * bytesPerRow);
  if (!s_readBuf) {
    s_src.close();
    s_out.close();
    Storage.remove(kStageTmpPath);
    return false;
  }

  s_header = header;
  s_payloadOffset = payloadOffset;
  s_bytesPerRow = bytesPerRow;
  s_band = 0;
  s_row = -1;  // band scratches not yet initialised
  s_powerLock = makeUniqueNoThrow<HalPowerManager::Lock>();
  LOG_INF("SSTG", "Prestaging sleep wallpaper: %s", header.path.c_str());
  return true;
}

pxc_plane::Geometry geometry() {
  pxc_plane::Geometry g;
  g.logicalWidth = s_header.pxcWidth;
  g.logicalHeight = s_header.pxcHeight;
  g.strideBytes = s_header.strideBytes;
  g.physicalHeight = s_header.phyHeight;
  return g;
}

void convertSlice() {
  const int bandRows = rowsInBand(s_band, s_header.phyHeight);
  const size_t scratchBytes = static_cast<size_t>(bandRows) * s_header.strideBytes;

  if (s_row < 0) {
    // New band: allocate + fill the plane scratches and rewind the payload.
    for (int p = 0; p < s_planeCount; p++) {
      if (!s_scratch[p]) s_scratch[p] = makeUniqueNoThrow<uint8_t[]>(scratchBytes);
      if (!s_scratch[p]) {
        abortConversion("scratch alloc failed");
        return;
      }
      memset(s_scratch[p].get(), pxc_plane::planeFill(planeForIndex(p, s_header.quality)), scratchBytes);
    }
    if (!s_src.seek(s_payloadOffset)) {
      abortConversion("payload seek failed");
      return;
    }
    s_row = 0;
  }

  // The pxc may carry the 1 px slack renderPxcSleepScreen allows; rows past
  // the framebuffer's bit width have no physical column to land in.
  const int maxRows = s_header.strideBytes * 8;
  const int totalRows = s_header.pxcHeight < maxRows ? s_header.pxcHeight : maxRows;
  const int rows = (totalRows - s_row < kRowsPerPump) ? (totalRows - s_row) : kRowsPerPump;
  const size_t bytes = static_cast<size_t>(rows) * s_bytesPerRow;
  if (s_src.read(s_readBuf.get(), bytes) != static_cast<int>(bytes)) {
    abortConversion("payload read failed");
    return;
  }
  const auto g = geometry();
  const int bandY0 = s_band * kBandRows;
  for (int r = 0; r < rows; r++) {
    const uint8_t* rowBuf = s_readBuf.get() + static_cast<size_t>(r) * s_bytesPerRow;
    for (int p = 0; p < s_planeCount; p++) {
      pxc_plane::packRowIntoBand(planeForIndex(p, s_header.quality), g, rowBuf, s_row + r, bandY0, bandRows,
                                 s_scratch[p].get());
    }
  }
  s_row += rows;

  if (s_row >= totalRows) {
    // Band complete: append the plane slices (band-major layout, pure appends).
    for (int p = 0; p < s_planeCount; p++) {
      if (s_out.write(s_scratch[p].get(), scratchBytes) != scratchBytes) {
        abortConversion("plane write failed");
        return;
      }
    }
    s_band++;
    s_row = -1;
    if (s_band * kBandRows >= s_header.phyHeight) {
      finishConversion();
    }
  }
}

}  // namespace

void pumpIdle(const uint16_t strideBytes, const uint16_t phyHeight) {
  switch (s_phase) {
    case Phase::Done:
      return;
    case Phase::Pending: {
      if (SETTINGS.wallpaperFormat != CrossPointSettings::WALLPAPER_PXC || !sleepScreenCanRouteToCustom()) {
        s_phase = Phase::Done;
        return;
      }
      // Never trigger the blocking index build from idle — wait for the
      // wallpaper index's own idle scan to settle first.
      if (!windex::idleComplete()) return;
      if (existingStageUsable(strideBytes, phyHeight)) {
        s_phase = Phase::Done;
        return;
      }
      // Skip prewarm while free memory is low so the prestage never grabs its
      // ~24 KB scratch out from under an active reader build. Stay Pending (do
      // not mark Done) so a later idle tick retries once the heap recovers.
      if (ESP.getFreeHeap() < kMinFreeHeapToStage || ESP.getMaxAllocHeap() < kMinBlockToStage) {
        return;
      }
      if (!startConversion(strideBytes, phyHeight)) {
        s_phase = Phase::Done;
        return;
      }
      s_phase = Phase::Converting;
      return;
    }
    case Phase::Converting:
      convertSlice();
      return;
  }
}

bool renderStaged(GfxRenderer& renderer) {
  if (!Storage.exists(kStagePath)) return false;
  HalFile f;
  if (!Storage.openFileForRead("SSTG", kStagePath, f)) return false;
  StageHeader h;
  const bool headerOk = readHeader(f, h) && h.complete == 1 &&
                        headerMatchesCurrentSetup(h, renderer.getDisplayWidthBytes(), renderer.getDisplayHeight());
  const size_t planeBytes = static_cast<size_t>(h.strideBytes) * h.phyHeight;
  if (!headerOk || planeBytes != renderer.getBufferSize()) {
    f.close();
    Storage.remove(kStagePath);
    return false;
  }

  const bool grayscale = h.quality == CrossPointSettings::SLEEP_IMG_PRETTY;
  const int planeCount = grayscale ? 3 : 1;
  // Band-major layout: reassemble one plane by reading its slice from each
  // band into the matching framebuffer rows.
  auto loadPlane = [&](const int idx) -> bool {
    uint8_t* fb = renderer.getFrameBuffer();
    for (int b = 0; b * kBandRows < h.phyHeight; b++) {
      const size_t sliceBytes = static_cast<size_t>(rowsInBand(b, h.phyHeight)) * h.strideBytes;
      const size_t off = bandDataOffset(b, planeCount, h.strideBytes, h.phyHeight) + idx * sliceBytes;
      if (!f.seek(off) ||
          f.read(fb + static_cast<size_t>(b) * kBandRows * h.strideBytes, sliceBytes) != static_cast<int>(sliceBytes)) {
        return false;
      }
    }
    return true;
  };

  if (!loadPlane(0)) {
    f.close();
    Storage.remove(kStagePath);
    return false;
  }
  renderer.setRenderMode(GfxRenderer::BW);
  drawSleepInfoOverlay(renderer, h.path);

  if (!grayscale) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
    // A failed LSB/MSB read past this point must not fall back to the
    // blocking path — the base refresh already repainted the panel — so
    // degrade to the silhouette-only image instead.
    if (loadPlane(1)) {
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
      drawSleepInfoOverlay(renderer, h.path);
      renderer.copyGrayscaleLsbBuffers();
      if (loadPlane(2)) {
        renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
        drawSleepInfoOverlay(renderer, h.path);
        renderer.copyGrayscaleMsbBuffers();
        renderer.displayGrayBuffer();
      }
    }
    renderer.setRenderMode(GfxRenderer::BW);
  }
  f.close();
  Storage.remove(kStagePath);
  LOG_INF("SLP", "rot engine=staged pick=%s", h.path.c_str());
  return true;
}

}  // namespace stage
}  // namespace sleep
}  // namespace crosspoint
