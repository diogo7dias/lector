#pragma once
#include <Arduino.h>
#include <DeviceProfile.h>
#include <EInkDisplay.h>
#include <WakeClassify.h>

class HalDisplay {
 public:
  // Constructor with pin configuration
  HalDisplay();

  // Destructor
  ~HalDisplay();

  // Refresh modes
  enum RefreshMode {
    FULL_REFRESH,  // Full refresh with complete waveform
    HALF_REFRESH,  // Half refresh (1720ms) - balanced quality and speed
    FAST_REFRESH   // Fast refresh using custom LUT
  };

  // Pass seamless=true on any path where the panel already shows the
  // content it should after begin() returns (silent reboot's popup,
  // sleep-wake with a restored buffer). Skips the wakeup-gated
  // requestResync() and defuses the SDK's X3 _x3InitialFullSyncsRemaining
  // counter; otherwise the first two paints get promoted to FULL
  // (~770ms each on X3). wakeupReason is setup()'s classification, passed in
  // so the boot is classified once rather than again here.
  void begin(bool seamless, wake_classify::WakeupReason wakeupReason);

  // Display dimensions
  static constexpr uint16_t DISPLAY_WIDTH = EInkDisplay::DISPLAY_WIDTH;
  static constexpr uint16_t DISPLAY_HEIGHT = EInkDisplay::DISPLAY_HEIGHT;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  // Frame buffer operations
  void clearScreen(uint8_t color = 0xFF) const;

  void displayBuffer(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);
  // Non-blocking refresh (shadow-free): starts the panel waveform and returns
  // while the panel refreshes on its own. The framebuffer must stay untouched
  // until waitRefreshComplete(), and the caller must rebuild the differential
  // baseline before the next differential update (the tiled grayscale cleanup
  // does). Panels without deferral fall back to a blocking refresh.
  void displayBufferAsync(RefreshMode mode = RefreshMode::FAST_REFRESH);
  // Block until a pending deferred refresh completes (no-op when none is).
  void waitRefreshComplete();
  // True when displayBufferAsync() genuinely overlaps (panel driver defers);
  // false where it falls back to a blocking refresh.
  bool supportsAsyncRefresh() const;

  // Output polarity. The framebuffer remains in normal polarity; inversion is
  // applied by the display driver while sending it to the panel.
  void setInverted(bool inverted);
  bool isInverted() const;

  // Power management
  void deepSleep();

  // Whether any refresh has finished since boot (the silent restart is waiting on its first frame).
  bool hasCompletedRefresh() const { return refreshCompleted; }

  // Drop the panel's high-voltage rails while leaving the image on the glass. The next
  // paint powers them back up on its own, so this is free to call whenever the device is
  // about to sit idle. Costs no flash and nothing visible. See PanelDriver::powerOff.
  void powerOffPanel();

  // Access to frame buffer
  uint8_t* getFrameBuffer() const;

  // Lend the framebuffer's ~48 KB STORAGE to a memory-hungry phase (chapter
  // builds) without freeing it: the allocation never moves, so repeated loans
  // cannot fragment the heap (free+realloc measurably did). No display calls
  // between lend and return; the panel keeps its last refreshed image. The
  // buffer comes back white — redraw fully. Returns nullptr if already lent.
  uint8_t* lendFrameBufferStorage(uint32_t* sizeOut);
  void returnFrameBufferStorage();

  // X3 grayscale preconditioning (OEM "AA-pre-BW(mid)" settle pass), windowed
  // to the gray region in physical panel coordinates (no-arg = full frame).
  // Call after the BW base frame is displayed and before the grayscale planes
  // are written; no-op on X4. See EInkDisplay::preconditionGrayscale.
  void preconditionGrayscale();
  void preconditionGrayscale(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

  // Display the framebuffer as the base frame for a grayscale overlay that
  // follows. On X3, HALF fallback first requests a resync to match
  // displayBuffer(HALF); FAST fallback keeps the OEM differential base waveform
  // ("AA-pre-BW(mid)"). Other panels display normally with `fallback` mode.
  void displayGrayscaleBase(RefreshMode fallback = HALF_REFRESH, bool turnOffScreen = false);

  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer);

  // The next FAST drives every pixel toward its target instead of only the pixels that
  // differ from the controller's old plane. For the first paint after a wake, when the
  // panel holds a sleep face the firmware cannot reconstruct: what is on the glass no
  // longer matters, and the page still costs one differential pass rather than a
  // clearing pass plus a paint. See PanelDriver::requestDriveAllNextFast.
  void driveAllPixelsNextFast();

  // fullTone: the pass is a whole-screen image (sleep wallpaper or cover) whose two mid
  // levels must stay apart. On the X4 Pro's UC8279 that swaps in a waveform with a
  // distinct dark and light grey; everywhere else it is ignored.
  void displayGrayBuffer(bool turnOffScreen = false, bool fullTone = false);

  // Tiled grayscale: stream one band of a plane (lsbPlane selects LSB/MSB RAM)
  // straight to the controller; supportsStripGrayscale() gates the path. See
  // EInkDisplay::writeGrayscalePlaneStrip.
  void writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t* rows, uint16_t yStart, uint16_t numRows);
  bool supportsStripGrayscale() const;

  // Runtime geometry passthrough
  uint16_t getDisplayWidth() const;
  uint16_t getDisplayHeight() const;
  uint16_t getDisplayWidthBytes() const;
  uint32_t getBufferSize() const;

  // Which device this is, as one value. Latched in begin(); every display rule that used
  // to spell the question its own way (gpio.deviceIsX3(), BoardConfig::isX4Pro(),
  // BoardConfig::ACTIVE.displayController) now reads this and passes it as a PARAMETER,
  // which is what makes those rules pure functions a host test can call.
  const DeviceProfile& profile() const { return deviceProfile; }

 private:
  DeviceProfile deviceProfile;
  EInkDisplay einkDisplay;

  // An async refresh has been started and waitRefreshComplete() has not yet seen it end.
  bool pendingAsync = false;
  // True once any refresh has finished since boot.
  bool refreshCompleted = false;
};

extern HalDisplay display;
