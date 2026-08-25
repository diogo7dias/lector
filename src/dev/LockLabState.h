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
// Every field is an index into the matching name table in LockLab.cpp, so the UI, the
// persistence and the render options all agree by construction. Defaults describe the
// shipped behaviour, so a freshly flashed kit renders what a release build renders until
// someone changes something.
struct LockLabState {
  uint8_t quality = 1;          // 0 Fast (1-bit), 1 Pretty (4-level)
  uint8_t dither = 0;           // PxcRenderOptions::Dither
  uint8_t levelMap = 0;         // preset index
  uint8_t invert = 0;
  uint8_t oneBitRefresh = 1;    // 0 Full, 1 Half, 2 Fast (HalDisplay::RefreshMode)
  uint8_t grayBaseRefresh = 0;  // 0 Auto, then Full / Half / Fast
  uint8_t passes = 0;           // PxcRenderOptions::Passes
  uint8_t preClear = 0;         // 0 Off, 1 White FULL, 2 Black then white, 3 Two cycles
  uint8_t wholeFileCache = 1;
  uint8_t rowsPerRead = 0;  // 0 Auto, then 1 / 4 / 8 / 16
  uint8_t source = 0;       // 0 /sleep.pxc, 1 /sleep folder, 2 /locklab folder
  uint8_t repeat = 0;       // 0 -> 1 run, 1 -> 3, 2 -> 5
  uint8_t overlay = 1;
  uint8_t realSleep = 0;  // 0 render only, 1 lock for real
  // Which file the folder sources hand over next, so stepping a matrix is one press per
  // image instead of a picker.
  uint16_t cursor = 0;
};

inline void lockLabToJson(const LockLabState& s, JsonDocument& doc) {
  JsonObject o = doc["lockLab"].to<JsonObject>();
  o["quality"] = s.quality;
  o["dither"] = s.dither;
  o["levelMap"] = s.levelMap;
  o["invert"] = s.invert;
  o["oneBitRefresh"] = s.oneBitRefresh;
  o["grayBaseRefresh"] = s.grayBaseRefresh;
  o["passes"] = s.passes;
  o["preClear"] = s.preClear;
  o["wholeFileCache"] = s.wholeFileCache;
  o["rowsPerRead"] = s.rowsPerRead;
  o["source"] = s.source;
  o["repeat"] = s.repeat;
  o["overlay"] = s.overlay;
  o["realSleep"] = s.realSleep;
  o["cursor"] = s.cursor;
}

inline void lockLabFromJson(LockLabState& s, JsonVariantConst doc) {
  JsonVariantConst o = doc["lockLab"];
  if (o.isNull()) return;
  s.quality = o["quality"] | s.quality;
  s.dither = o["dither"] | s.dither;
  s.levelMap = o["levelMap"] | s.levelMap;
  s.invert = o["invert"] | s.invert;
  s.oneBitRefresh = o["oneBitRefresh"] | s.oneBitRefresh;
  s.grayBaseRefresh = o["grayBaseRefresh"] | s.grayBaseRefresh;
  s.passes = o["passes"] | s.passes;
  s.preClear = o["preClear"] | s.preClear;
  s.wholeFileCache = o["wholeFileCache"] | s.wholeFileCache;
  s.rowsPerRead = o["rowsPerRead"] | s.rowsPerRead;
  s.source = o["source"] | s.source;
  s.repeat = o["repeat"] | s.repeat;
  s.overlay = o["overlay"] | s.overlay;
  s.realSleep = o["realSleep"] | s.realSleep;
  s.cursor = o["cursor"] | s.cursor;
}

#endif  // LECTOR_LOCK_LAB
