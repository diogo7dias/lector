#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

struct EInkCallLog {
  std::vector<std::string> events;
  void clear() { events.clear(); }
};

inline EInkCallLog& einkCallLog() {
  static EInkCallLog log;
  return log;
}

class EInkDisplay {
 public:
  static constexpr uint16_t DISPLAY_WIDTH = 800;
  static constexpr uint16_t DISPLAY_HEIGHT = 480;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };
  enum GrayPlane { GRAY_PLANE_LSB, GRAY_PLANE_MSB };

  EInkDisplay(int8_t, int8_t, int8_t, int8_t, int8_t, int8_t) { std::memset(fb, 0xAA, sizeof(fb)); }

  void setDisplayX3() {}
  void begin() {}
  void skipInitialResync() {}
  void requestResync(uint8_t passes = 0) { einkCallLog().events.push_back("requestResync:" + std::to_string(passes)); }
  void requestDriveAllNextFast() {}

  void clearScreen(uint8_t color) const {
    std::memset(const_cast<uint8_t*>(fb), color, sizeof(fb));
    einkCallLog().events.push_back("clearScreen");
  }

  void displayBuffer(RefreshMode mode, bool turnOffScreen) {
    einkCallLog().events.push_back(std::string("displayBuffer:") + modeName(mode) + (turnOffScreen ? ":off" : ""));
  }

  void displayBufferAsyncNoShadow(RefreshMode mode) {
    einkCallLog().events.push_back(std::string("displayBufferAsyncNoShadow:") + modeName(mode));
  }

  void waitRefreshComplete() { einkCallLog().events.push_back("waitRefreshComplete"); }
  bool supportsAsyncRefresh() const { return true; }

  void setInverted(bool inv) {
    inverted = inv;
    einkCallLog().events.push_back(inv ? "setInverted:1" : "setInverted:0");
  }
  bool isInverted() const { return inverted; }

  void deepSleep() { einkCallLog().events.push_back("deepSleep"); }
  void powerOffPanel() { einkCallLog().events.push_back("powerOffPanel"); }

  uint8_t* getFrameBuffer() const { return lent ? nullptr : const_cast<uint8_t*>(fb); }
  uint8_t* lendBuildStorage(uint32_t* sizeOut) {
    einkCallLog().events.push_back("lendBuildStorage");
    lent = true;
    if (sizeOut) *sizeOut = sizeof(fb);
    return fb;
  }
  void returnBuildStorage() { lent = false; }

  void displayGrayscaleBase(RefreshMode fallback, bool) {
    einkCallLog().events.push_back(std::string("displayGrayscaleBase:") + modeName(fallback));
  }
  void preconditionGrayscale(uint16_t, uint16_t, uint16_t, uint16_t) {
    einkCallLog().events.push_back("preconditionGrayscale");
  }
  void copyGrayscaleLsbBuffers(const uint8_t*) { einkCallLog().events.push_back("copyGrayscaleLsb"); }
  void copyGrayscaleMsbBuffers(const uint8_t*) { einkCallLog().events.push_back("copyGrayscaleMsb"); }
  void cleanupGrayscaleBuffers(const uint8_t* bw) {
    einkCallLog().events.push_back(bw == fb ? "cleanupGrayscaleBuffers:fb" : "cleanupGrayscaleBuffers:other");
  }
  void displayGrayBuffer(bool, const unsigned char*) { einkCallLog().events.push_back("displayGrayBuffer"); }
  void writeGrayscalePlaneStrip(GrayPlane, const uint8_t*, uint16_t, uint16_t) {
    einkCallLog().events.push_back("writeGrayscalePlaneStrip");
  }
  bool supportsStripGrayscale() const { return true; }

  uint16_t getDisplayWidth() const { return DISPLAY_WIDTH; }
  uint16_t getDisplayHeight() const { return DISPLAY_HEIGHT; }
  uint16_t getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
  uint32_t getBufferSize() const { return sizeof(fb); }

 private:
  static const char* modeName(RefreshMode m) {
    switch (m) {
      case FULL_REFRESH:
        return "FULL";
      case HALF_REFRESH:
        return "HALF";
      default:
        return "FAST";
    }
  }
  uint8_t fb[16]{};
  bool inverted = false;
  bool lent = false;
};
