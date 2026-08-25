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
const char* const kLevelMap[] = {
    "Identity",     "Lift blacks",  "Crush whites", "High contrast",
    "Darken lines", "Heavy ink",    "Bright mids",  "Ink on white",
    "Dim whites",   "Compress mids","Split mids",   "Lift shadows",
    "Ink only",     "Airy",         "No black",     "Two tone soft"};
const char* const kOffOn[] = {"Off", "On"};
const char* const kRefresh[] = {"Full", "Half", "Fast"};
const char* const kRefreshAuto[] = {"Auto", "Full", "Half", "Fast"};
const char* const kPasses[] = {"All three", "Base only", "Planes only"};
const char* const kPreClear[] = {"Off", "White FULL", "Black then white", "Two cycles"};
const char* const kRowsPerRead[] = {"Auto", "1", "4", "8", "16"};
const char* const kSource[] = {"/sleep.pxc", "/sleep folder", "/locklab folder"};
const char* const kRepeat[] = {"1 run", "3 runs", "5 runs"};
const char* const kRealSleep[] = {"Render only", "Full lock", "Render then lock"};

// The tone curves. A .pxc carries four levels and nothing else, so a four-entry table is
// the entire curve available: anything a converter can do to the midtones after the fact
// is some permutation of these four numbers.
constexpr uint8_t kLevelMaps[16][4] = {
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
    // The eight above move the picture. The eight below map the corners of what four
    // levels can express at all, so a tester can find the ceiling rather than guess at it.
    {0, 1, 2, 2},  // Dim whites: paper white down a step. Less glare under a frontlight,
                   // and it shows whether the panel's white is the thing that is too pale.
    {0, 1, 1, 3},  // Compress mids: both midtones to the darker one. A denser picture with
                   // one fewer grey.
    {0, 2, 2, 3},  // Split mids: both midtones to the lighter one. The same trade upward.
    {1, 1, 2, 3},  // Lift shadows: true black never asked for. If the ghost survives this
                   // unchanged, the ghost is not coming from the ink plane.
    {0, 0, 0, 3},  // Ink only: everything but paper white goes to ink. The most black the
                   // format can put on the glass.
    {1, 2, 3, 3},  // Airy: every level up one. The brightest reading of the same file.
    {2, 2, 3, 3},  // No black: level 0 never driven. A control for the base pass, since a
                   // ghost that persists here cannot be blamed on black.
    {1, 1, 3, 3},  // Two tone soft: two levels, neither of them extreme. High contrast
                   // without the hard edges.
};

constexpr uint16_t kRowsPerReadValues[5] = {0, 1, 4, 8, 16};

const Knob kKnobs[] = {
    {"Quality", &LockLabState::quality, 2, kQuality},
    {"Dither (1-bit)", &LockLabState::dither, 4, kDither},
    {"Tone curve", &LockLabState::levelMap, 16, kLevelMap},
    {"Invert", &LockLabState::invert, 2, kOffOn},
    {"1-bit refresh", &LockLabState::oneBitRefresh, 3, kRefresh},
    {"Gray base refresh", &LockLabState::grayBaseRefresh, 4, kRefreshAuto},
    {"Gray passes", &LockLabState::passes, 3, kPasses},
    {"Pre-clear", &LockLabState::preClear, 4, kPreClear},
    {"Cache whole file", &LockLabState::wholeFileCache, 2, kOffOn},
    {"Rows per read", &LockLabState::rowsPerRead, 5, kRowsPerRead},
    {"Source", &LockLabState::source, 3, kSource},
    {"Repeat", &LockLabState::repeat, 3, kRepeat},
    {"Info overlay", &LockLabState::overlay, 2, kOffOn},
    {"On Run", &LockLabState::realSleep, 3, kRealSleep},
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

// A clean pass before the render, which the X4 Pro sleep path has never had.
//
// The pre-sleep full clean was removed on purpose (SleepActivity.cpp:705) and replaced by
// the anti-ghost budget, which promotes only every thirteenth FAST pass to a clean one. On
// a UC8279 X4 Pro the grayscale base is already promoted to FULL, so a ghost that survives
// it is not the base's to fix: displayGrayBuffer paints a differential FAST-class waveform
// over whatever the panel already holds. This is the knob that tests whether an explicit
// scrub first is what the panel actually needs, and what that scrub costs.
uint32_t applyPreClear(GfxRenderer& renderer) {
  const uint8_t mode = APP_STATE.lockLab.preClear;
  if (mode == 0) return 0;
  const uint32_t startMs = millis();
  const int cycles = (mode == 3) ? 2 : 1;
  for (int i = 0; i < cycles; i++) {
    if (mode >= 2) {
      // Black first. A ghost is trapped charge, and driving every pixel to the opposite
      // rail is the only thing that reliably shifts it; white alone leaves white-on-white
      // history untouched.
      renderer.fillRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(), true);
      renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    }
    renderer.clearScreen();
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  }
  return millis() - startMs;
}

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
  memcpy(o.levelMap, kLevelMaps[state.levelMap < 16 ? state.levelMap : 0], sizeof(o.levelMap));
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
  uint32_t preClearMs = 0;
  for (int i = 0; i < runs; i++) {
    preClearMs = applyPreClear(renderer);
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
           "%s %s dither=%s tone=%s inv=%d 1bit=%s graybase=%s passes=%s preclear=%s(%ums) cache=%d rows=%s | "
           "runs=%d min=%u avg=%u max=%u [%s]",
           fileName, kQuality[state.quality], kDither[state.dither], kLevelMap[state.levelMap], state.invert ? 1 : 0,
           kRefresh[state.oneBitRefresh], kRefreshAuto[state.grayBaseRefresh], kPasses[state.passes],
           kPreClear[state.preClear], static_cast<unsigned>(preClearMs), state.wholeFileCache ? 1 : 0, kRowsPerRead[state.rowsPerRead], ok, static_cast<unsigned>(best),
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
