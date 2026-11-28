#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>

#if FREEINK_DEVICE_X4PRO
#include <BoardConfig.h>

#include <array>
#endif

// Global HalDisplay instance
HalDisplay display;

#define SD_SPI_MISO 7

HalDisplay::HalDisplay() : einkDisplay(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY) {}

HalDisplay::~HalDisplay() {}

void HalDisplay::begin(bool seamless, const wake_classify::WakeupReason wakeupReason) {
  // The one read of the hardware detect. Everything below, and the two other X3 branches
  // in this file, take their answer from this latched value rather than the global.
  deviceProfile = deviceProfileFromHardware();

  if (deviceProfile.isX3) {
    einkDisplay.setDisplayX3();
  }

  einkDisplay.begin();
  einkDisplay.setBusyWaitHooks([]() { powerManager.beginBusyWait(); }, []() { powerManager.endBusyWait(); });

  if (seamless) {
    // Defuse the SDK's X3 _x3InitialFullSyncsRemaining counter (no-op on X4)
    // so the first paint isn't promoted to FULL (~770ms). Skips the wakeup-
    // gated requestResync() below for the same reason.
    einkDisplay.skipInitialResync();
    return;
  }
  // Request resync after specific wakeup events to ensure clean display state.
  using wake_classify::WakeupReason;
  if (wakeupReason == WakeupReason::PowerButton || wakeupReason == WakeupReason::AfterFlash ||
      wakeupReason == WakeupReason::Other) {
    einkDisplay.requestResync();
  }
}

void HalDisplay::clearScreen(uint8_t color) const { einkDisplay.clearScreen(color); }

EInkDisplay::RefreshMode convertRefreshMode(HalDisplay::RefreshMode mode) {
  switch (mode) {
    case HalDisplay::FULL_REFRESH:
      return EInkDisplay::FULL_REFRESH;
    case HalDisplay::HALF_REFRESH:
      return EInkDisplay::HALF_REFRESH;
    case HalDisplay::FAST_REFRESH:
    default:
      return EInkDisplay::FAST_REFRESH;
  }
}

void HalDisplay::displayBuffer(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  if (deviceProfile.isX3 && mode == RefreshMode::HALF_REFRESH) {
    einkDisplay.requestResync(1);
  }

  einkDisplay.displayBuffer(convertRefreshMode(mode), turnOffScreen);
  refreshCompleted = true;
}

void HalDisplay::displayBufferAsync(HalDisplay::RefreshMode mode) {
  pendingAsync = true;
  if (deviceProfile.isX3 && mode == RefreshMode::HALF_REFRESH) {
    einkDisplay.requestResync(1);
  }

  einkDisplay.displayBufferAsyncNoShadow(convertRefreshMode(mode));
}

void HalDisplay::waitRefreshComplete() {
  einkDisplay.waitRefreshComplete();
  if (pendingAsync) {
    refreshCompleted = true;
    pendingAsync = false;
  }
}

bool HalDisplay::supportsAsyncRefresh() const { return einkDisplay.supportsAsyncRefresh(); }

void HalDisplay::setInverted(bool inverted) { einkDisplay.setInverted(inverted); }

bool HalDisplay::isInverted() const { return einkDisplay.isInverted(); }

void HalDisplay::deepSleep() { einkDisplay.deepSleep(); }

// Safe to call at any time and as often as you like: the driver keeps its own power flag
// and returns immediately when the rails are already down, so a repeat costs no bus traffic.
void HalDisplay::powerOffPanel() { einkDisplay.powerOffPanel(); }

uint8_t* HalDisplay::getFrameBuffer() const { return einkDisplay.getFrameBuffer(); }

uint8_t* HalDisplay::lendFrameBufferStorage(uint32_t* sizeOut) { return einkDisplay.lendBuildStorage(sizeOut); }

void HalDisplay::returnFrameBufferStorage() { einkDisplay.returnBuildStorage(); }

void HalDisplay::displayGrayscaleBase(RefreshMode fallback, bool turnOffScreen) {
  // X3: a HALF fallback means the caller wants a clean base (e.g. the sleep
  // cover, a full-screen swap from arbitrary prior content). Without this, the
  // X3 grayscale base takes its gentle differential happy path and the prior
  // home/reader frame ghosts through the soft aa_pre_bw_mid waveform. Forcing a
  // resync makes displayGrayscaleBase clear first, matching displayBuffer(HALF).
  // The reader's FAST path is deliberately left on the differential path so
  // per-page grayscale stays cheap.
  if (deviceProfile.isX3 && fallback == RefreshMode::HALF_REFRESH) {
    einkDisplay.requestResync(1);
  }

  einkDisplay.displayGrayscaleBase(convertRefreshMode(fallback), turnOffScreen);
  refreshCompleted = true;
}

void HalDisplay::preconditionGrayscale() { preconditionGrayscale(0, 0, getDisplayWidth(), getDisplayHeight()); }

void HalDisplay::preconditionGrayscale(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  // Finish any prior async refresh before this separate conditioning pass.
  waitRefreshComplete();
  einkDisplay.preconditionGrayscale(x, y, w, h);
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) { einkDisplay.copyGrayscaleLsbBuffers(lsbBuffer); }

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) { einkDisplay.copyGrayscaleMsbBuffers(msbBuffer); }

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) { einkDisplay.cleanupGrayscaleBuffers(bwBuffer); }

void HalDisplay::driveAllPixelsNextFast() { einkDisplay.requestDriveAllNextFast(); }

#if FREEINK_DEVICE_X4PRO
// X4 Pro UC8279: a separate dark and light grey for whole-screen images.
//
// The stock AA waveform (kXtfAa02 / kXtfAa68, freeink-sdk Uc8279X4Driver.cpp:57-70) gives
// its two mixed-plane tables, 0x22 and 0x23, byte-identical data. Those are exactly the
// tables the driver's absolute fold sends level 1 (dark) and level 2 (light) to
// (Uc8279X4Driver.h:89-95), and both levels start from the same black base pixel, so they
// get the same drive and land on the same grey: a .pxc or cover shows three tones, not
// four. Fine for text anti-aliasing, which is what that waveform was cut for.
//
// The split keeps the stock bytes and gives the dark grey its white push in phase D
// (stock: one idle frame on every table); light keeps phase B. The two greys cannot differ
// by frame count inside one phase: a phase's frame count is shared by all five tables (the
// stock tables agree column by column), only the level bits are per table. Layout of each
// 49-byte table: [0]=0x01, [1..4]=phases A-D (bits 7:6 level, 0x80 white push, 0x40 black
// push; bits 5:0 frames), [5],[6],[12],[13]=0x01, the rest 0.
//
// The two greys sit either side of the stock one, not below it. Stock pushes both for the
// phase-B count (3 frames on LUT_VER 0x68, 2 on 0x02), and that count is the one judged best
// on hardware: the LUT Lab swept it up to 4x and every variant looked worse (0.31.18 notes).
// A split that leaves light at the stock count and cuts dark to one frame averages darker
// than that, which reads as crushed shadows and a dim picture; stretching one grey a frame
// up and the other a frame down keeps the mean where hardware put it.
//
// Calibration knobs, per build with -D:
//   LECTOR_X4PRO_SPLIT_MIDS   0 = stock tables (A/B against the split).
//   LECTOR_X4PRO_DARK_LESS    frames the dark push is shorter than the stock count (1).
//   LECTOR_X4PRO_LIGHT_MORE   frames the light push is longer than the stock count (1).
//   LECTOR_X4PRO_SWAP_MIDS    1 if the panel shows the two greys the wrong way round.
#ifndef LECTOR_X4PRO_SPLIT_MIDS
#define LECTOR_X4PRO_SPLIT_MIDS 1
#endif
#ifndef LECTOR_X4PRO_DARK_LESS
#define LECTOR_X4PRO_DARK_LESS 1
#endif
#ifndef LECTOR_X4PRO_LIGHT_MORE
#define LECTOR_X4PRO_LIGHT_MORE 1
#endif
#ifndef LECTOR_X4PRO_SWAP_MIDS
#define LECTOR_X4PRO_SWAP_MIDS 0
#endif
// The 0x02 table's stock count is 2, so a dark push may lose at most one frame.
static_assert(LECTOR_X4PRO_DARK_LESS >= 0 && LECTOR_X4PRO_DARK_LESS < 2, "dark push needs at least one frame");
static_assert(LECTOR_X4PRO_LIGHT_MORE >= 0 && LECTOR_X4PRO_LIGHT_MORE < 60, "6-bit frame count");

namespace {
constexpr size_t kAaLen = 49;
using SplitGrayLut = std::array<uint8_t, 5 * kAaLen>;

// Rows in register order 0x20..0x24: VCOM, then the black, dark, light and white pixels'
// tables under the driver's inverted absolute planes.
constexpr SplitGrayLut splitGrayLut(const uint8_t stock) {
  const uint8_t light = static_cast<uint8_t>(stock + LECTOR_X4PRO_LIGHT_MORE);  // phase B
  const uint8_t dark = static_cast<uint8_t>(stock - LECTOR_X4PRO_DARK_LESS);    // phase D
  constexpr uint8_t phaseC[5] = {0x01, 0x41, 0x01, 0x01, 0x81};                 // stock endpoint pushes
  constexpr size_t darkRow = LECTOR_X4PRO_SWAP_MIDS ? 3 : 2;
  constexpr size_t lightRow = LECTOR_X4PRO_SWAP_MIDS ? 2 : 3;
  SplitGrayLut t{};
  for (size_t r = 0; r < 5; r++) {
    const size_t o = r * kAaLen;
    t[o + 0] = 0x01;
    t[o + 1] = 0x02;
    t[o + 2] = static_cast<uint8_t>((r == lightRow ? 0x80 : 0x00) | light);
    t[o + 3] = phaseC[r];
    t[o + 4] = static_cast<uint8_t>((r == darkRow ? 0x80 : 0x00) | dark);
    t[o + 5] = t[o + 6] = t[o + 12] = t[o + 13] = 0x01;
  }
  return t;
}

// Flash-resident, one per LUT_VER; the argument is that table's stock phase-B count.
constexpr SplitGrayLut kSplitGray02 = splitGrayLut(2);
constexpr SplitGrayLut kSplitGray68 = splitGrayLut(3);

// The load-bearing invariant: each phase runs one frame count on all five tables, and the
// two mid rows really differ.
constexpr bool phasesAgree(const SplitGrayLut& t) {
  for (size_t p = 1; p <= 4; p++)
    for (size_t r = 1; r < 5; r++)
      if ((t[r * kAaLen + p] & 0x3F) != (t[p] & 0x3F)) return false;
  return t[2 * kAaLen + 2] != t[3 * kAaLen + 2] && t[2 * kAaLen + 4] != t[3 * kAaLen + 4];
}
static_assert(!LECTOR_X4PRO_SPLIT_MIDS || (phasesAgree(kSplitGray02) && phasesAgree(kSplitGray68)),
              "split grey LUT breaks the shared per-phase frame count");
}  // namespace
#endif

void HalDisplay::displayGrayBuffer(bool turnOffScreen, bool fullTone) {
  // Null keeps the driver's own waveform. The override is UC8279-X4 bytes only: an X4 Pro
  // that probes as SSD1677 must never be handed it.
  const unsigned char* lut = nullptr;
#if FREEINK_DEVICE_X4PRO
  if (LECTOR_X4PRO_SPLIT_MIDS && fullTone && deviceProfile.controllerIsUc8279 && !deviceProfile.isX3) {
    // 0x03 is the same AA pair as 0x02: match the SDK's bank choice (Uc8279X4Driver.cpp).
    const uint8_t v = BoardConfig::ACTIVE.displayControllerVariant;
    lut = (v == 0x02 || v == 0x03 ? kSplitGray02 : kSplitGray68).data();
  }
#else
  (void)fullTone;
#endif
  einkDisplay.displayGrayBuffer(turnOffScreen, lut);
  refreshCompleted = true;
}

void HalDisplay::writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t* rows, uint16_t yStart, uint16_t numRows) {
  einkDisplay.writeGrayscalePlaneStrip(lsbPlane ? EInkDisplay::GRAY_PLANE_LSB : EInkDisplay::GRAY_PLANE_MSB, rows,
                                       yStart, numRows);
}

bool HalDisplay::supportsStripGrayscale() const { return einkDisplay.supportsStripGrayscale(); }

uint16_t HalDisplay::getDisplayWidth() const { return einkDisplay.getDisplayWidth(); }

uint16_t HalDisplay::getDisplayHeight() const { return einkDisplay.getDisplayHeight(); }

uint16_t HalDisplay::getDisplayWidthBytes() const { return einkDisplay.getDisplayWidthBytes(); }

uint32_t HalDisplay::getBufferSize() const { return einkDisplay.getBufferSize(); }
