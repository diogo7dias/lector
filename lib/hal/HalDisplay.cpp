#include <HalDisplay.h>
#include <HalGPIO.h>
#include <PerfLog.h>

// Global HalDisplay instance
HalDisplay display;

#define SD_SPI_MISO 7

HalDisplay::HalDisplay() : einkDisplay(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY) {}

HalDisplay::~HalDisplay() {}

void HalDisplay::begin(bool seamless) {
  // Set X3-specific panel mode before initializing.
  if (gpio.deviceIsX3()) {
    einkDisplay.setDisplayX3();
  }

  einkDisplay.begin();

  if (seamless) {
    // Defuse the SDK's X3 _x3InitialFullSyncsRemaining counter (no-op on X4)
    // so the first paint isn't promoted to FULL (~770ms). Skips the wakeup-
    // gated requestResync() below for the same reason.
    einkDisplay.skipInitialResync();
    return;
  }
  // Request resync after specific wakeup events to ensure clean display state.
  const auto wakeupReason = gpio.getWakeupReason();
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton || wakeupReason == HalGPIO::WakeupReason::AfterFlash ||
      wakeupReason == HalGPIO::WakeupReason::Other) {
    einkDisplay.requestResync();
  }
}

void HalDisplay::clearScreen(uint8_t color) const { einkDisplay.clearScreen(color); }

void HalDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           bool fromProgmem) const {
  einkDisplay.drawImage(imageData, x, y, w, h, fromProgmem);
}

void HalDisplay::drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                      bool fromProgmem) const {
  einkDisplay.drawImageTransparent(imageData, x, y, w, h, fromProgmem);
}

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

// Anti-ghosting cap, restored from the pre-rebase fork, where it was the reason the
// sleep wallpaper never ghosted. The upstream base has no equivalent: it lets an
// unbounded run of FAST (differential) passes accumulate, and content held still for
// minutes then traps charge deep enough that a full inversion at lock cannot undo it.
// The device evidence was that locking after a cold boot is clean while locking after
// a session ghosts, with the lock's wipe visibly running either way — i.e. the wipe
// was never the missing piece; the cap was. Promoting every 13th FAST to a clean pass
// costs one flash per 13 page turns and keeps the panel from ever getting that deep.
HalDisplay::RefreshMode HalDisplay::applyRefreshPolicy(const RefreshMode requested) {
  DisplayRefreshPolicy::Mode policyMode = DisplayRefreshPolicy::Mode::Fast;
  if (requested == RefreshMode::HALF_REFRESH) {
    policyMode = DisplayRefreshPolicy::Mode::Clean;
  } else if (requested == RefreshMode::FULL_REFRESH) {
    policyMode = DisplayRefreshPolicy::Mode::Full;
  }

  switch (refreshPolicy.choose(policyMode, millis())) {
    case DisplayRefreshPolicy::Mode::Clean:
      return RefreshMode::HALF_REFRESH;
    case DisplayRefreshPolicy::Mode::Full:
      return RefreshMode::FULL_REFRESH;
    case DisplayRefreshPolicy::Mode::Fast:
    default:
      return RefreshMode::FAST_REFRESH;
  }
}

// An X3 HALF is not one waveform. HalDisplay asks the driver to resync first, which turns
// it into a white-baseline full sync plus a conditioning pass: measured at 2551 ms on an
// X3, against 2016 ms for an outright FULL and 617 ms for a FAST.
//
// That strength is right when a caller asks for HALF on purpose — the sleep screen wants
// a genuinely clean panel under the wallpaper. It is wrong for a pass the anti-ghost cap
// promoted out of a FAST. The policy's own design has two tiers: a cheap Clean every 12
// FAST passes, and a real FULL every 48 because "a Clean pass does not discharge the
// panel". The resync was collapsing the cheap tier into the expensive one, so the tier
// that exists to be affordable was costing more than the tier that exists to be thorough.
//
// So: resync only when the caller genuinely asked for HALF. A promoted pass takes the
// driver's plain half scrub, which still drives every pixel to its target ignoring the
// previous frame — a real clean, just not a full discharge. The every-48 FULL remains the
// discharge, exactly as the policy intends.
bool HalDisplay::needsX3HalfResync(const RefreshMode requested, const RefreshMode actual) const {
  return gpio.deviceIsX3() && actual == RefreshMode::HALF_REFRESH && requested == RefreshMode::HALF_REFRESH;
}

void HalDisplay::displayBuffer(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  const RefreshMode requested = mode;
  const uint32_t startUs = micros();
  mode = applyRefreshPolicy(mode);
  if (needsX3HalfResync(requested, mode)) {
    einkDisplay.requestResync(1);
  }

  einkDisplay.displayBuffer(convertRefreshMode(mode), turnOffScreen);
  // Blocking path: the whole cost is in one call, so there is no async split to report.
  PerfLog::record(requested, mode, micros() - startUs, 0);
}

void HalDisplay::displayBufferAsync(HalDisplay::RefreshMode mode) {
  const RefreshMode requested = mode;
  pendingAsyncStartUs = micros();
  pendingAsyncRequested = mode;
  mode = applyRefreshPolicy(mode);
  pendingAsyncActual = mode;
  pendingAsync = true;
  if (needsX3HalfResync(requested, mode)) {
    einkDisplay.requestResync(1);
  }

  // A promoted pass has multi-phase post-work, so run it synchronously rather than
  // detached — same trade the pre-rebase fork made.
  if (mode != requested && mode != RefreshMode::FAST_REFRESH) {
    einkDisplay.displayBuffer(convertRefreshMode(mode), false);
    // Ran blocking despite the async request, so it has no split to report and no wait
    // for waitRefreshComplete() to time.
    PerfLog::record(requested, mode, micros() - pendingAsyncStartUs, 0);
    pendingAsync = false;
    return;
  }

  einkDisplay.displayBufferAsyncNoShadow(convertRefreshMode(mode));
  // Time to here is the part that genuinely overlaps: commands issued and bytes pushed,
  // with the waveform fired but not waited on. waitRefreshComplete() closes the record.
  pendingAsyncSplitUs = micros() - pendingAsyncStartUs;
}

void HalDisplay::waitRefreshComplete() {
  einkDisplay.waitRefreshComplete();
  if (pendingAsync) {
    PerfLog::record(pendingAsyncRequested, pendingAsyncActual, micros() - pendingAsyncStartUs, pendingAsyncSplitUs);
    pendingAsync = false;
  }
}

bool HalDisplay::supportsAsyncRefresh() const { return einkDisplay.supportsAsyncRefresh(); }

void HalDisplay::refreshDisplay(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  const RefreshMode requested = mode;
  const uint32_t startUs = micros();
  mode = applyRefreshPolicy(mode);
  if (needsX3HalfResync(requested, mode)) {
    einkDisplay.requestResync(1);
  }

  einkDisplay.refreshDisplay(convertRefreshMode(mode), turnOffScreen);
  PerfLog::record(requested, mode, micros() - startUs, 0);
}

void HalDisplay::deepSleep() {
  refreshPolicy.reset();
  einkDisplay.deepSleep();
}

void HalDisplay::setBusyWaitSliceHook(bool (*sliceHook)(int8_t busyPin, uint8_t busyLevel)) {
  einkDisplay.setBusyWaitSliceHook(sliceHook);
}

uint8_t* HalDisplay::getFrameBuffer() const { return einkDisplay.getFrameBuffer(); }

uint8_t* HalDisplay::lendFrameBufferStorage(uint32_t* sizeOut) { return einkDisplay.lendBuildStorage(sizeOut); }

void HalDisplay::returnFrameBufferStorage() { einkDisplay.returnBuildStorage(); }

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  einkDisplay.copyGrayscaleBuffers(lsbBuffer, msbBuffer);
}

void HalDisplay::displayGrayscaleBase(RefreshMode fallback, bool turnOffScreen) {
  fallback = applyRefreshPolicy(fallback);
  // X3: a HALF fallback means the caller wants a clean base (e.g. the sleep
  // cover, a full-screen swap from arbitrary prior content). Without this, the
  // X3 grayscale base takes its gentle differential happy path and the prior
  // home/reader frame ghosts through the soft aa_pre_bw_mid waveform. Forcing a
  // resync makes displayGrayscaleBase clear first, matching displayBuffer(HALF).
  // The reader's FAST path is deliberately left on the differential path so
  // per-page grayscale stays cheap.
  if (gpio.deviceIsX3() && fallback == RefreshMode::HALF_REFRESH) {
    einkDisplay.requestResync(1);
  }

  einkDisplay.displayGrayscaleBase(convertRefreshMode(fallback), turnOffScreen);
}

void HalDisplay::preconditionGrayscale() { einkDisplay.preconditionGrayscale(); }

void HalDisplay::preconditionGrayscale(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  einkDisplay.preconditionGrayscale(x, y, w, h);
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) { einkDisplay.copyGrayscaleLsbBuffers(lsbBuffer); }

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) { einkDisplay.copyGrayscaleMsbBuffers(msbBuffer); }

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) { einkDisplay.cleanupGrayscaleBuffers(bwBuffer); }

void HalDisplay::displayGrayBuffer(bool turnOffScreen) { einkDisplay.displayGrayBuffer(turnOffScreen); }

void HalDisplay::writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t* rows, uint16_t yStart, uint16_t numRows) {
  einkDisplay.writeGrayscalePlaneStrip(lsbPlane ? EInkDisplay::GRAY_PLANE_LSB : EInkDisplay::GRAY_PLANE_MSB, rows,
                                       yStart, numRows);
}

bool HalDisplay::supportsStripGrayscale() const { return einkDisplay.supportsStripGrayscale(); }

uint16_t HalDisplay::getDisplayWidth() const { return einkDisplay.getDisplayWidth(); }

uint16_t HalDisplay::getDisplayHeight() const { return einkDisplay.getDisplayHeight(); }

uint16_t HalDisplay::getDisplayWidthBytes() const { return einkDisplay.getDisplayWidthBytes(); }

uint32_t HalDisplay::getBufferSize() const { return einkDisplay.getBufferSize(); }
