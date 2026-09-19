#pragma once

#include <cstddef>
#include <cstdint>

// Where the seconds go on a wake.
//
// Milestones feed the optional previous-wake SD record. Current-wake serial
// diagnostics run independently and measure through destination render completion.
// Static RAM only; no additional SD writes or heap allocations.
namespace WakeTiming {

// Ordered milestones. Each is stamped as milliseconds since boot, so a stage's cost is
// the difference between neighbours. Keep in sync with kStageNames in the .cpp.
enum class Stage : uint8_t {
  // The four below split what used to be one "HalReady" stage. On an X3 it measured
  // 430 ms of a 2010 ms wake and was not even printed: it is the FIRST stamp, so every
  // later stage is a delta from it and its own cost never appeared in the readout. That
  // made the second largest item in the wake invisible. Splitting it showed 272 ms still
  // sitting before the first stamp, so the baseline moved earlier again: "the framework's
  // own startup" and "our Serial.begin" are different answers and only one of them is
  // ours to fix.
  SerialUp = 0,     // Serial.begin() returned; everything before it is the framework's
  SysReady = 1,     // HalSystem::begin() returned
  GpioReady = 2,    // gpio.begin() returned, so the ADC button ladder is powered
  HalReady = 3,     // power manager and clock up, before the SD card
  SdReady = 4,      // Storage.begin() returned
  ConfigReady = 5,  // settings, state, recents, OPDS and presets loaded
  // DisplayReady before InputSettled, because that is the order they now run in: the
  // display comes up INSIDE the button-ladder settle window rather than after it. These
  // must stay in chronological order — each stage is printed as a delta from the one
  // before, so a stamp out of order reports as zero and folds its cost into its neighbour.
  DisplayReady = 6,   // setupDisplayAndFonts() returned
  InputSettled = 7,   // power-button verify plus the recovery-mode button settle window
  WakeFaceReady = 8,  // splash finished, or painted-face clear strategy armed
  // The two below split the route into the reader. ReaderActivity::onEnter loads the
  // font and opens the book inline before ActivityUp; an SD font family is re-read from
  // the card on every wake (deep sleep is a chip reset), so this is where a slow unlock
  // with a card font shows up. Unset on a wake that lands on home.
  FontLoaded = 9,   // the reader's SD font family is resident (or was already built in)
  BookLoaded = 10,  // the book's index is open, the page reader activity is queued
  ActivityUp = 11,  // setup() routed and returned; the page itself paints on the next loop()
  Count = 12,
};

// Current wake serial diagnostics are independent of the optional SD record.
// Logs is the card trace and perf log opening their session files: card lookups that
// scale with how many old sessions are on the card, so it gets its own figure.
enum class Cost : uint8_t { Classify, Config, Logs, Settle, Count };
void noteCost(Cost cost, uint32_t ms);
// Called on the render task, after a destination render that submitted a frame.
void readable();
// Called on the main task at dispatch: first call reports readiness, first accepted
// event reports the full wake line. User think time is included only in first_input.
// Returns the event unchanged; called by the existing input queries, never polls.
bool noteAcceptedInput(bool accepted);
void reportInput();

// Switches the card read and write on. Off (the default) means loadPrevious() and
// persist() do nothing, so a stable build pays no SD write per wake for numbers nobody
// is going to look at. mark() is always live: it is two stores into a small array, and
// making it conditional would only add a branch.
//
// Call before loadPrevious(), i.e. after the settings have been read.
void setEnabled(bool enabled);

// Stamp `stage` with the current millis(). Safe to call before begin(); calls that arrive
// out of order are kept as-is, since the reader of these numbers wants the raw stamps.
void mark(Stage stage);

// Clear the slate for the wake starting now. Call as the FIRST statement of setup(),
// before the first mark(). It only zeroes two small arrays, so it is safe that early, and
// the first stamp has to be able to land before anything else has run.
void beginWake();

// Read the previous wake's stamps from the SD card. Call once, right after the card is
// mounted (Stage::SdReady) and before the wake diagnostics are logged — the numbers cannot
// be reported before they have been loaded.
//
// The card, not RTC memory: the X3 cuts power to the RTC block on sleep, so nothing
// stored there survives. See the note at the top of the .cpp.
void loadPrevious();

// Write this wake's stamps to the card, for the next wake to log. Call once at the
// end of the wake, after the last mark(). One 22-byte record, one write per wake.
void persist();

// Human-readable breakdown of the PREVIOUS wake, e.g. "pre 190 sys 8 gpio 82 hal 4 sd 61
// cfg 129 in 310 disp 138 push 710 act 232 = 2010". The leading "pre" is everything
// before the first stamp — the framework's own startup and Serial — which has no stage of
// its own to be a delta from and would otherwise only show up as the gap between the sum
// and the total. Returns an empty string when no previous wake was recorded,
// which is the case on the first boot after a flash.
//
// The buffer is filled rather than returned by value to keep this off the heap during a
// wake. Pass at least 160 bytes.
void formatPrevious(char* out, size_t outLen);

// Like formatPrevious, but NEVER returns an empty string.
//
// Reports either the recorded timings or why no previous record is available.
//
// Pass at least 160 bytes.
void formatDiagnostic(char* out, size_t outLen);

}  // namespace WakeTiming
