#include <HalDisplay.h>
#include <HalGPIO.h>
#include <PerfLog.h>
#include <PerfStats.h>

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
HalDisplay::RefreshMode HalDisplay::applyRefreshPolicy(const RefreshMode requested, const uint16_t inkScore,
                                                       const bool allowTurbo) {
  DisplayRefreshPolicy::Mode policyMode = DisplayRefreshPolicy::Mode::Fast;
  if (requested == RefreshMode::HALF_REFRESH) {
    policyMode = DisplayRefreshPolicy::Mode::Clean;
  } else if (requested == RefreshMode::FULL_REFRESH) {
    policyMode = DisplayRefreshPolicy::Mode::Full;
  }

  // Decide the fast path before the mode, because the choice changes what this pass
  // costs the panel and so can pull a clean forward. Only a FAST can be Turbo; a
  // promoted pass runs its own absolute waveform and ignores the request anyway.
  // The driver is told every pass, not only when the answer changes: it holds the
  // setting, and the periodic reload depends on it going back to Standard on cue.
  // Gated on the panel honouring it, not merely on the request: on a driver that ignores
  // FastQuality the request reaches nothing, and recording it would put a 1 in the perf
  // log's turbo column for a pass that ran the ordinary waveform.
  const bool turboPossible = turboWanted && einkDisplay.supportsFastTurbo();
  lastPassWasTurbo =
      allowTurbo && policyMode == DisplayRefreshPolicy::Mode::Fast && refreshPolicy.useTurbo(turboPossible);
  einkDisplay.setFastQuality(lastPassWasTurbo ? EInkDisplay::FAST_TURBO : EInkDisplay::FAST_STANDARD);

  switch (refreshPolicy.choose(policyMode, millis(), inkScore, lastPassWasTurbo)) {
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

// The frame is only worth measuring when the answer can change what happens. Only a FAST
// request can be promoted by the ink debt; a caller that already asked for HALF or FULL is
// getting a clean pass regardless, and the ~2 ms pass over the framebuffer would buy
// nothing. The metrics state is still updated on those paths — through the driver-side
// reset below — so the next FAST is scored against what is really on the glass.
uint16_t HalDisplay::scoreFrame(const RefreshMode requested) {
  const uint8_t* const fb = einkDisplay.getFrameBuffer();
  const FrameInkMetrics::Result result =
      inkMetrics.update(fb, einkDisplay.getDisplayWidthBytes(), einkDisplay.getDisplayHeight());
  if (requested != RefreshMode::FAST_REFRESH) return 0;

  // Inverted output (night mode) is applied by the driver on its way to the panel, so the
  // framebuffer this scored is in normal polarity and the score understates what the panel
  // will actually be asked to do: a dark page drives nearly every pixel. Charge it double,
  // capped at the scale's own maximum.
  uint32_t score = result.score;
  if (einkDisplay.isInverted()) score *= 2;
  return static_cast<uint16_t>(score > FrameInkMetrics::MAX_SCORE ? FrameInkMetrics::MAX_SCORE : score);
}

// One place both instrumentation sinks are fed from, so a refresh path can never end up
// in the card log but missing from the on-panel overlay (or the reverse). PerfStats is
// unconditional and costs a handful of integer updates; PerfLog returns immediately
// unless the timings setting opened a file for it.
void HalDisplay::noteRefreshTiming(const RefreshMode requested, const RefreshMode actual, const uint32_t totalUs,
                                   const uint32_t asyncStartUs, const uint16_t thinkMs, const uint16_t inkScore) const {
  const uint16_t debt = refreshPolicy.inkDebt();
  // Read once, here, so every refresh path reports the split without having to remember
  // to. The counters were armed by beginRefreshAccounting() at the top of that path.
  // Every path that reports must have armed them on entry, or it reports the PREVIOUS
  // refresh's split alongside its own total. All five do: displayBuffer,
  // displayBufferAsync, refreshDisplay, displayGrayscaleBase, displayGrayBuffer.
  const uint32_t wireUs = EInkDisplay::refreshTransferMicros();
  const uint32_t waveUs = EInkDisplay::refreshBusyMicros();
  PerfStats::noteRefresh(requested, actual, totalUs, asyncStartUs, thinkMs, inkScore, debt, wireUs, waveUs);
  PerfLog::record(requested, actual, totalUs, asyncStartUs, thinkMs, inkScore, debt, wireUs, waveUs, lastPassWasTurbo);
}

void HalDisplay::displayBuffer(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  const RefreshMode requested = mode;
  EInkDisplay::resetRefreshAccounting();
  const uint32_t startUs = micros();
  const uint16_t thinkMs = PerfStats::takeThinkMs(millis());
  const uint16_t inkScore = scoreFrame(requested);
  mode = applyRefreshPolicy(mode, inkScore);
  if (needsX3HalfResync(requested, mode)) {
    einkDisplay.requestResync(1);
  }

  einkDisplay.displayBuffer(convertRefreshMode(mode), turnOffScreen);
  // Blocking path: the whole cost is in one call, so there is no async split to report.
  noteRefreshTiming(requested, mode, micros() - startUs, 0, thinkMs, inkScore);
}

void HalDisplay::displayBufferAsync(HalDisplay::RefreshMode mode) {
  const RefreshMode requested = mode;
  EInkDisplay::resetRefreshAccounting();
  pendingAsyncStartUs = micros();
  // Taken at the start, not at completion: the press this paint answers is outstanding
  // now, and waitRefreshComplete() may be called long after a later press has landed.
  pendingAsyncThinkMs = PerfStats::takeThinkMs(millis());
  pendingAsyncRequested = mode;
  pendingAsyncInkScore = scoreFrame(requested);
  mode = applyRefreshPolicy(mode, pendingAsyncInkScore);
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
    noteRefreshTiming(requested, mode, micros() - pendingAsyncStartUs, 0, pendingAsyncThinkMs, pendingAsyncInkScore);
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
    noteRefreshTiming(pendingAsyncRequested, pendingAsyncActual, micros() - pendingAsyncStartUs, pendingAsyncSplitUs,
                      pendingAsyncThinkMs, pendingAsyncInkScore);
    pendingAsync = false;
  }
}

bool HalDisplay::supportsAsyncRefresh() const { return einkDisplay.supportsAsyncRefresh(); }

void HalDisplay::refreshDisplay(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  const RefreshMode requested = mode;
  EInkDisplay::resetRefreshAccounting();
  const uint32_t startUs = micros();
  const uint16_t thinkMs = PerfStats::takeThinkMs(millis());
  mode = applyRefreshPolicy(mode);
  if (needsX3HalfResync(requested, mode)) {
    einkDisplay.requestResync(1);
  }

  einkDisplay.refreshDisplay(convertRefreshMode(mode), turnOffScreen);
  noteRefreshTiming(requested, mode, micros() - startUs, 0, thinkMs, 0);
}

void HalDisplay::setInverted(bool inverted) { einkDisplay.setInverted(inverted); }

bool HalDisplay::toggleInverted() { return einkDisplay.toggleInverted(); }

bool HalDisplay::isInverted() const { return einkDisplay.isInverted(); }

void HalDisplay::deepSleep() {
  // The panel is about to hold whatever the sleep screen left on it, and the next boot
  // starts with an empty framebuffer. Nothing measured in this session describes what the
  // next one will find on the glass.
  inkMetrics.reset();
  // The budget the caller wants to carry across the lock has already been read out by
  // then (see enterDeepSleep); this reset only clears the in-RAM copy the reset would
  // have wiped anyway.
  refreshPolicy.reset();
  einkDisplay.deepSleep();
}

// Safe to call at any time and as often as you like: the driver keeps its own power flag
// and returns immediately when the rails are already down, so a repeat costs no bus traffic.
void HalDisplay::powerOffPanel() { einkDisplay.powerOffPanel(); }

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
  const RefreshMode requested = fallback;
  EInkDisplay::resetRefreshAccounting();
  const uint32_t startUs = micros();
  const uint16_t thinkMs = PerfStats::takeThinkMs(millis());
  // Never the cheap path: this frame is the base a grayscale overlay is about to be
  // driven onto, so it has to land exactly where the planes expect it.
  fallback = applyRefreshPolicy(fallback, 0, /*allowTurbo=*/false);
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
  // Timed like any other refresh: on the X3 wake path and the reader's image pages this
  // IS the paint the reader waits for, and leaving it out of the log made those screens
  // look free.
  noteRefreshTiming(requested, fallback, micros() - startUs, 0, thinkMs, 0);
}

void HalDisplay::preconditionGrayscale() { einkDisplay.preconditionGrayscale(); }

void HalDisplay::preconditionGrayscale(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  einkDisplay.preconditionGrayscale(x, y, w, h);
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) { einkDisplay.copyGrayscaleLsbBuffers(lsbBuffer); }

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) { einkDisplay.copyGrayscaleMsbBuffers(msbBuffer); }

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) { einkDisplay.cleanupGrayscaleBuffers(bwBuffer); }

// The grayscale planes go straight to the driver: there is no refresh mode to choose,
// the waveform is the gray nudge. They still drive the panel and still leave charge, so
// they spend the same anti-ghost budget a FAST pass does — otherwise a page with images
// or text anti-aliasing ages the panel while the budget stands still.
void HalDisplay::displayGrayBuffer(bool turnOffScreen) {
  EInkDisplay::resetRefreshAccounting();
  const uint32_t startUs = micros();
  const uint16_t thinkMs = PerfStats::takeThinkMs(millis());
  refreshPolicy.noteExternalFastPass();
  einkDisplay.displayGrayBuffer(turnOffScreen);
  // Recorded as FAST/FAST: there is no mode to choose here, and charging it to the same
  // bucket keeps the per-mode totals comparable with a text page turn.
  noteRefreshTiming(RefreshMode::FAST_REFRESH, RefreshMode::FAST_REFRESH, micros() - startUs, 0, thinkMs,
                    DisplayRefreshPolicy::EXTERNAL_PASS_SCORE);
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
