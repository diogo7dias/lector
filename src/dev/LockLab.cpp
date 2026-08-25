#ifdef LECTOR_LOCK_LAB

#include "dev/LockLab.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PerfLog.h>

#include <cstdio>
#include <cstring>

#include "CrossPointState.h"
#include "SleepTiming.h"
#include "activities/boot_sleep/SleepInfoOverlay.h"

namespace locklab {
namespace {

const char* const kQuality[] = {"Fast (1-bit)", "Pretty (4-level)"};
const char* const kDither[] = {"Bayer 2x2", "Bayer 4x4", "Blue noise", "Threshold"};
const char* const kLevelMap[] = {"Identity",     "Lift blacks", "Crush whites", "High contrast",
                                 "Darken lines", "Heavy ink",   "Bright mids",  "Ink on white"};
const char* const kOffOn[] = {"Off", "On"};
const char* const kRefresh[] = {"Full", "Half", "Fast"};
const char* const kRefreshAuto[] = {"Auto", "Full", "Half", "Fast"};
const char* const kPasses[] = {"All three", "Base only", "Planes only"};
const char* const kRowsPerRead[] = {"Auto", "1", "4", "8", "16"};
const char* const kSource[] = {"/sleep.pxc", "/sleep folder", "/locklab folder"};
const char* const kRepeat[] = {"1 run", "3 runs", "5 runs"};
const char* const kRealSleep[] = {"Render only", "Full lock"};

// The tone curves. A .pxc carries four levels and nothing else, so a four-entry table is
// the entire curve available: anything a converter can do to the midtones after the fact
// is some permutation of these four numbers.
constexpr uint8_t kLevelMaps[8][4] = {
    {0, 1, 2, 3},  // Identity: the image as the encoder made it
    {1, 2, 2, 3},  // Lift blacks: shadow detail out of a picture that reads as a blob
    {0, 1, 3, 3},  // Crush whites: highlights to paper white, for a washed-out scan
    {0, 0, 3, 3},  // High contrast: the midtones gone, which is what the panel does anyway
                   // when the base pass is wrong, so it doubles as a control
    {0, 0, 2, 3},  // Darken lines: level 1 to ink, midtone kept. Line art whose strokes
                   // read grey rather than black, without flattening the background.
    {0, 0, 1, 3},  // Heavy ink: as above and the midtone dropped a step. A background that
                   // is too pale to sit behind white subject matter.
    {0, 2, 3, 3},  // Bright mids: level 1 up to the midtone. Opens a picture that lost its
                   // shadow detail to a base pass darker than the encoder assumed.
    {0, 3, 3, 3},  // Ink on white: only true black survives. Maximum separation for pen
                   // and ink, and the widest black-to-white span the format can express.
};

constexpr uint16_t kRowsPerReadValues[5] = {0, 1, 4, 8, 16};

const Knob kKnobs[] = {
    {"Quality", &LockLabState::quality, 2, kQuality},
    {"Dither (1-bit)", &LockLabState::dither, 4, kDither},
    {"Tone curve", &LockLabState::levelMap, 8, kLevelMap},
    {"Invert", &LockLabState::invert, 2, kOffOn},
    {"1-bit refresh", &LockLabState::oneBitRefresh, 3, kRefresh},
    {"Gray base refresh", &LockLabState::grayBaseRefresh, 4, kRefreshAuto},
    {"Gray passes", &LockLabState::passes, 3, kPasses},
    {"Cache whole file", &LockLabState::wholeFileCache, 2, kOffOn},
    {"Rows per read", &LockLabState::rowsPerRead, 5, kRowsPerRead},
    {"Source", &LockLabState::source, 3, kSource},
    {"Repeat", &LockLabState::repeat, 3, kRepeat},
    {"Info overlay", &LockLabState::overlay, 2, kOffOn},
    {"On Run", &LockLabState::realSleep, 2, kRealSleep},
};

bool pendingFullLock = false;

// The n-th .pxc in a directory, counted in FAT order. Deliberately a fresh walk each
// time rather than a cached listing: a matrix folder is tens of files, the walk costs
// nothing at that size, and a stale cache after the tester swaps the card would be a
// confusing way to lose an afternoon.
std::string pxcAt(const char* dirPath, uint16_t wanted, uint16_t& outCount) {
  outCount = 0;
  std::string found;
  HalFile dir = Storage.open(dirPath, O_RDONLY);
  if (!dir || !dir.isDirectory()) return found;
  char name[256];  // FAT long-file-name maximum (255 chars + terminator)
  dir.rewindDirectory();
  for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    const bool isDir = entry.isDirectory();
    entry.getName(name, sizeof(name));
    entry.close();
    if (isDir || !hasPxcExtension(name)) continue;
    if (outCount == wanted) {
      found = std::string(dirPath) + "/" + name;
    }
    ++outCount;
  }
  return found;
}

}  // namespace

const Knob* knobs() { return kKnobs; }

size_t knobCount() { return sizeof(kKnobs) / sizeof(kKnobs[0]); }

const char* knobValueName(const Knob& knob, const LockLabState& state) {
  const uint8_t at = state.*(knob.field);
  return knob.names[at < knob.count ? at : 0];
}

void cycleKnob(const Knob& knob, LockLabState& state) {
  uint8_t& at = state.*(knob.field);
  at = static_cast<uint8_t>((at + 1) % knob.count);
}

PxcRenderOptions optionsFor(const LockLabState& state) {
  PxcRenderOptions o;
  o.dither = static_cast<PxcRenderOptions::Dither>(state.dither);
  o.passes = static_cast<PxcRenderOptions::Passes>(state.passes);
  memcpy(o.levelMap, kLevelMaps[state.levelMap < 8 ? state.levelMap : 0], sizeof(o.levelMap));
  o.invert = state.invert != 0;
  // Position 0 is Auto, which is the -1 the renderer reads as "ask
  // sleepGrayscaleBaseRefresh()"; the rest line up with HalDisplay::RefreshMode.
  o.grayBaseRefresh = (state.grayBaseRefresh == 0) ? -1 : static_cast<int8_t>(state.grayBaseRefresh - 1);
  o.wholeFileCache = state.wholeFileCache != 0;
  o.rowsPerRead = kRowsPerReadValues[state.rowsPerRead < 5 ? state.rowsPerRead : 0];
  return o;
}

std::string nextSourcePath(LockLabState& state) {
  if (state.source == 0) return std::string("/sleep.pxc");
  const char* dir = (state.source == 1) ? "/sleep" : "/locklab";
  uint16_t total = 0;
  std::string path = pxcAt(dir, state.cursor, total);
  if (total == 0) return std::string();
  if (path.empty()) {
    // The cursor ran past the end, which happens whenever the folder shrinks under it.
    state.cursor = 0;
    path = pxcAt(dir, 0, total);
  }
  state.cursor = static_cast<uint16_t>((state.cursor + 1) % (total == 0 ? 1 : total));
  return path;
}

void runOnce(GfxRenderer& renderer, char* const out, const size_t outLen) {
  if (outLen == 0) return;
  out[0] = '\0';
  LockLabState& state = APP_STATE.lockLab;

  const std::string path = nextSourcePath(state);
  if (path.empty()) {
    snprintf(out, outLen, "no .pxc found for source %s", kSource[state.source < 3 ? state.source : 0]);
    LOG_INF("LAB", "%s", out);
    return;
  }

  const PxcRenderOptions options = optionsFor(state);
  const bool grayscale = state.quality != 0;
  const auto oneBitRefresh = static_cast<HalDisplay::RefreshMode>(state.oneBitRefresh);
  const int runs = (state.repeat == 0) ? 1 : ((state.repeat == 1) ? 3 : 5);

  uint32_t best = 0xFFFFFFFF, worst = 0, total = 0;
  int ok = 0;
  char stages[320];
  stages[0] = '\0';
  for (int i = 0; i < runs; i++) {
    SleepTiming::begin();
    const uint32_t startMs = millis();
    bool rendered;
    {
      const SleepInfoOverlayScope overlayScope(path);
      rendered = renderPxcSleepScreen(renderer, path, grayscale, oneBitRefresh,
                                      state.overlay != 0 ? &drawSleepInfoOverlay : nullptr, &options);
    }
    const uint32_t elapsed = millis() - startMs;
    // The last run's breakdown, not an average of them: the stage names only mean
    // anything attached to one render, and a mean would hide the run that stalled.
    SleepTiming::format(stages, sizeof(stages));
    if (!rendered) {
      snprintf(out, outLen, "render failed: %s", path.c_str());
      LOG_ERR("LAB", "%s", out);
      return;
    }
    ++ok;
    total += elapsed;
    if (elapsed < best) best = elapsed;
    if (elapsed > worst) worst = elapsed;
  }

  // One line carrying the whole experiment: which file, every knob, and what it cost.
  // A kit log is read days later by someone who was not in the room for the settings.
  const char* const fileName = strrchr(path.c_str(), '/') ? strrchr(path.c_str(), '/') + 1 : path.c_str();
  snprintf(out, outLen,
           "%s %s dither=%s tone=%s inv=%d 1bit=%s graybase=%s passes=%s cache=%d rows=%s | "
           "runs=%d min=%u avg=%u max=%u [%s]",
           fileName, kQuality[state.quality], kDither[state.dither], kLevelMap[state.levelMap], state.invert ? 1 : 0,
           kRefresh[state.oneBitRefresh], kRefreshAuto[state.grayBaseRefresh], kPasses[state.passes],
           state.wholeFileCache ? 1 : 0, kRowsPerRead[state.rowsPerRead], ok, static_cast<unsigned>(best),
           static_cast<unsigned>(total / (ok == 0 ? 1 : ok)), static_cast<unsigned>(worst), stages);
  LOG_INF("LAB", "%s", out);
  PerfLog::note(out);
  PerfLog::flush();
  APP_STATE.saveToFile();
}

void requestFullLock() { pendingFullLock = true; }

bool takePendingFullLock() {
  const bool was = pendingFullLock;
  pendingFullLock = false;
  return was;
}

}  // namespace locklab

#endif  // LECTOR_LOCK_LAB
