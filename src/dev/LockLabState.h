#pragma once

#ifdef LECTOR_LOCK_LAB

#include <ArduinoJson.h>

#include <cstdint>

// What the Lock Lab is currently set to.
//
// It lives in CrossPointState rather than CrossPointSettings for one reason: a lock test
// ends in deep sleep, and CrossPointState is already written on the way there
// (src/main.cpp, APP_STATE.saveToFile()). Knobs held only in RAM would not survive the
// very thing they exist to measure.
//
// Every field is an index the render options are built from (LockLab.cpp), and the
// defaults are the recipe chosen on glass: Identity tone curve, a FULL grayscale base,
// and a white FULL pre-clear before the render. The JSON key carries a version suffix so
// a device that already holds the old block falls back to these rather than resurrecting
// whatever was last cycled by hand.
struct LockLabState {
  uint8_t dither = 0;    // PxcRenderOptions::Dither
  uint8_t levelMap = 0;  // preset index
  uint8_t invert = 0;
  uint8_t grayBaseRefresh = 1;  // 0 Auto, then Full / Half / Fast
  uint8_t passes = 0;           // PxcRenderOptions::Passes
  uint8_t preClear = 1;         // 0 Off, 1 White FULL, 2 Black then white, 3 Two cycles
  uint8_t wholeFileCache = 1;
  uint8_t rowsPerRead = 0;  // 0 Auto, then 1 / 4 / 8 / 16
};

inline void lockLabToJson(const LockLabState& s, JsonDocument& doc) {
  JsonObject o = doc["lockLab2"].to<JsonObject>();
  o["dither"] = s.dither;
  o["levelMap"] = s.levelMap;
  o["invert"] = s.invert;
  o["grayBaseRefresh"] = s.grayBaseRefresh;
  o["passes"] = s.passes;
  o["preClear"] = s.preClear;
  o["wholeFileCache"] = s.wholeFileCache;
  o["rowsPerRead"] = s.rowsPerRead;
}

inline void lockLabFromJson(LockLabState& s, JsonVariantConst doc) {
  JsonVariantConst o = doc["lockLab2"];
  if (o.isNull()) return;
  s.dither = o["dither"] | s.dither;
  s.levelMap = o["levelMap"] | s.levelMap;
  s.invert = o["invert"] | s.invert;
  s.grayBaseRefresh = o["grayBaseRefresh"] | s.grayBaseRefresh;
  s.passes = o["passes"] | s.passes;
  s.preClear = o["preClear"] | s.preClear;
  s.wholeFileCache = o["wholeFileCache"] | s.wholeFileCache;
  s.rowsPerRead = o["rowsPerRead"] | s.rowsPerRead;
}

#endif  // LECTOR_LOCK_LAB
