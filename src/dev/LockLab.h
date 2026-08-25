#pragma once

#ifdef LECTOR_LOCK_LAB

#include <cstddef>
#include <cstdint>
#include <string>

#include "activities/boot_sleep/PxcSleepRenderer.h"
#include "dev/LockLabState.h"

class GfxRenderer;

// The Lock Lab: a bench for the lock screen and the .pxc pipeline.
//
// A lock costs about ten seconds and the perf marks say where the time goes, but they
// cannot say what any of it buys. This turns the interesting variables into knobs a
// tester can change on the device, renders a wallpaper with them, and reports both what
// it cost and, because the picture is on the panel, what it looks like. It exists to be
// deleted once the questions it was built to answer are answered.
//
// Its strings are plain English literals rather than tr() keys on purpose: the screen is
// never in a release build, and the string tables generated from the translation YAML are
// not #ifdef-aware, so a lab key would cost flash in every shipped firmware.
namespace locklab {

// One knob: a label, the LockLabState field it moves, and the names of its positions.
struct Knob {
  const char* label;
  uint8_t LockLabState::* field;
  uint8_t count;
  const char* const* names;
};

// The knob table, in display order. The list screen is this plus one Run row.
const Knob* knobs();
size_t knobCount();

// The name of a knob's current position, for the right-hand column of its row.
const char* knobValueName(const Knob& knob, const LockLabState& state);

// Steps a knob one position, wrapping.
void cycleKnob(const Knob& knob, LockLabState& state);

// The render options the current knobs describe.
PxcRenderOptions optionsFor(const LockLabState& state);

// Which file the current source setting hands over next. Empty when there is nothing to
// render, in which case the caller reports that rather than rendering a failure.
std::string nextSourcePath(LockLabState& state);

// Drives every pixel to the opposite rail before a render, as the Pre-clear knob asks,
// and returns what it cost in milliseconds. Zero when the knob is Off.
//
// Called from both paths on purpose. The bench needs it to measure the cost, and a Full
// lock needs it or the knob does nothing on the one path a tester actually watches.
uint32_t applyPreClear(GfxRenderer& renderer);

// Renders one wallpaper with the current knobs, repeated as many times as the Repeat knob
// asks, and writes a one-line summary of the recipe and the timings into `out`. The
// summary also reaches the serial log and the perf CSV, so a kit log carries the whole
// experiment without the card having to be read.
void runOnce(GfxRenderer& renderer, char* out, size_t outLen);

// True when the tester asked for a real lock rather than a render, which the sleep path
// checks so the measured number is a genuine lock. Cleared by the check.
bool takePendingFullLock();
void requestFullLock();

}  // namespace locklab

#endif  // LECTOR_LOCK_LAB
