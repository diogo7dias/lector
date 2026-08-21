#pragma once

#include <cstddef>
#include <cstdint>

// Where the milliseconds go, on any build.
//
// PerfLog (lib/PerfLog) is the record: one CSV line per refresh, written to the card for
// later reading. This is the live counterpart — a few hundred bytes of in-RAM totals the
// device can draw on its own panel while it is being used, so a change can be judged
// without pulling the card out first.
//
// It carries one number the panel timings alone cannot give: THINK TIME, the gap between
// the button press and the refresh call it caused. A menu move that feels slow is either
// think time (the firmware was busy deciding what to draw) or panel time (the ink was
// slow), and those two have nothing in common as problems. Measuring them apart is the
// whole reason this exists.
//
// Everything here is static storage: no heap, no card, no timer, and no work at all
// beyond a handful of integer updates per refresh.
namespace PerfStats {

// Mode indices match HalDisplay::RefreshMode (FULL, HALF, FAST).
constexpr uint8_t kModeCount = 3;

// The button press landed. Called once per accepted input event, before the activity
// gets to react to it. A second call before any refresh replaces the first: what matters
// is the press the paint answers, and in a burst that is the last one.
void noteInput(uint32_t ms);

// Milliseconds from that press to now, consumed. Returns kNoThink when no press is
// outstanding, which is the ordinary case for a refresh nobody asked for (a background
// build finishing, the battery banner, a timed sleep).
constexpr uint16_t kNoThink = 0xFFFF;
uint16_t takeThinkMs(uint32_t nowMs);

// One completed refresh. `totalUs` is the whole call, `asyncStartUs` the part that
// returned before the panel had finished (0 on a blocking refresh). `thinkMs` is the
// value takeThinkMs() returned for this refresh, so the overlay can show the pair.
// `inkScore` is what FrameInkMetrics made of the frame (0..1000) and `inkDebt` the running
// total the anti-ghost policy holds after it. Both are reported because the thresholds
// that turn them into a clean pass can only be tuned against real content.
// `wireUs` is the time spent streaming the frame into controller RAM and `waveUs` the time
// spent waiting on BUSY while the panel drives. Whatever the total does not account for is
// host work. The three have completely different fixes — a faster bus, a different
// waveform, less work per frame — and one elapsed number cannot tell them apart. A
// measured X4 FAST was 773 ms flat while the same panel's sleep FAST was 367 ms, and
// nothing in the totals said where the other 406 ms went.
void noteRefresh(uint8_t requestedMode, uint8_t actualMode, uint32_t totalUs, uint32_t asyncStartUs, uint16_t thinkMs,
                 uint16_t inkScore, uint16_t inkDebt, uint32_t wireUs, uint32_t waveUs);

// Session totals of that split, for the end-of-session summary written to the card.
void splitTotals(uint64_t& wireUs, uint64_t& waveUs, uint64_t& totalUs);

// One line for the on-panel overlay, describing the PREVIOUS refresh: a refresh cannot
// report its own cost inside the frame it is drawing. Fills `out`; pass at least 96 bytes.
//
// Abbreviated to fit the narrow panel axis (480 px on X4). Reading order:
//   FAST/HALF  requested mode / mode actually run
//   t41        think, ms from the button press to the refresh call ("-" if none)
//   p623       panel, the whole refresh call in ms
//   w96 v500   of that panel time, wire (frame into controller RAM) and waveform (BUSY)
//   s96        async split: the part that returned before the panel finished (omitted if 0)
//   ink302/3800  this frame's ink score, and the anti-ghost debt after it
//   pr3        refreshes promoted out of FAST so far this session
//   pll09      the X3 frame-clock byte in force (meaningless on X4, shown anyway)
void formatLastLine(char* out, size_t outLen);

// The accumulated table, one line per mode that has run, for the diagnostics screen.
// Returns the number of lines written. Each line needs ~48 bytes.
size_t formatSummary(char (*lines)[64], size_t maxLines);

// Everything since boot, per mode.
struct ModeStats {
  uint32_t count = 0;
  uint32_t lastUs = 0;
  uint32_t minUs = 0;
  uint32_t maxUs = 0;
  uint64_t sumUs = 0;
};
const ModeStats& modeStats(uint8_t mode);

// How many refreshes asked for FAST and were given something slower by the anti-ghost
// policy. A page turn that occasionally costs two seconds is this number's fault, and
// seeing it climb is how a policy change is judged.
uint32_t promotedCount();

// Whether holding a button down still costs one panel refresh per press.
//
// The render task is notified per update request and takes the whole pending count in one
// go, so a burst that lands while a refresh is in flight is meant to collapse into a
// single repaint of the final state. noteRenderPass() is called once per repaint with the
// number of requests that repaint served; renderPassCount() and updateRequestCount() then
// say how well that worked over a session. Equal numbers mean nothing ever coalesced and
// the input path is serialising on the panel after all.
void noteRenderPass(uint32_t requestsServed);
uint32_t renderPassCount();
uint32_t updateRequestCount();

// The X3 frame-clock byte in force this session, for the overlay and the CSV header.
// Meaningless on X4, where the panel runs its own OTP waveform; reported anyway rather
// than hidden, so one overlay format serves both devices.
void setPllByte(uint8_t pll);
uint8_t pllByte();

}  // namespace PerfStats
