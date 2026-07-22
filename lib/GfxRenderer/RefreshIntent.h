#pragma once

#include <cstdint>

// RefreshIntent — the semantic reason a caller wants the panel updated, as opposed
// to the mechanism (FULL/HALF/FAST waveform) it used to hand-pick. Callers say WHAT
// (a page turn, a menu redraw, a clean frame, a progress tick); the mapping below
// says HOW. The X3-vs-X4 panel quirks (windowed->full on X3, X3+HALF resync) stay
// where they already live and are device-proven: inside the HalDisplay verbs that
// present() dispatches to. This header is pure (no HalDisplay/Arduino), so the
// intent->plan table is host-testable (test/refresh_intent) — the behavior-
// preservation contract for the migration off ~80 hand-picked refresh calls.
enum class RefreshIntent : uint8_t {
  MenuNav,               // snappy full-screen redraw (menus, dialogs, most screens)
  PageTurn,              // detached full-panel page flip (reader), policy may promote
  CleanFrame,            // ghost-free clean paint (banners, sleep faces, error frames)
  DeepClean,             // full inversion pre-pass over arbitrary prior content
  ProgressBar,           // windowed bar / banner strip (full-panel FAST on X3)
  TransientBand,         // detached small overlay band (full-panel async on X3)
  GrayscaleClean,        // grayscale base that must clean first (HALF fallback)
  GrayscaleDifferential  // grayscale base kept on the cheap differential (FAST fallback)
};

// Which underlying refresh verb an intent dispatches to.
enum class RefreshVerb : uint8_t { Buffer, BufferAsync, Window, WindowAsync, GrayscaleBase };

// Waveform class the verb runs (maps to HalDisplay::RefreshMode in present()). For
// the async/window verbs the wave is the FAST baseline the anti-ghosting policy may
// still promote.
enum class RefreshWave : uint8_t { Fast, Half, Full };

struct RefreshPlan {
  RefreshVerb verb;
  RefreshWave wave;
};

// The single source of truth for intent -> (verb, waveform). Every row reproduces
// the exact behavior of the call sites it replaces (see test/refresh_intent and the
// migration catalog). constexpr so it folds at compile time and stays in flash.
constexpr RefreshPlan refreshPlanFor(const RefreshIntent intent) {
  switch (intent) {
    case RefreshIntent::MenuNav:
      return {RefreshVerb::Buffer, RefreshWave::Fast};
    case RefreshIntent::PageTurn:
      return {RefreshVerb::BufferAsync, RefreshWave::Fast};
    case RefreshIntent::CleanFrame:
      return {RefreshVerb::Buffer, RefreshWave::Half};
    case RefreshIntent::DeepClean:
      return {RefreshVerb::Buffer, RefreshWave::Full};
    case RefreshIntent::ProgressBar:
      return {RefreshVerb::Window, RefreshWave::Fast};
    case RefreshIntent::TransientBand:
      return {RefreshVerb::WindowAsync, RefreshWave::Fast};
    case RefreshIntent::GrayscaleClean:
      return {RefreshVerb::GrayscaleBase, RefreshWave::Half};
    case RefreshIntent::GrayscaleDifferential:
      return {RefreshVerb::GrayscaleBase, RefreshWave::Fast};
  }
  // Unreachable for the enum's domain; keep the compiler happy without a default so
  // adding an intent triggers a -Wswitch warning at every switch that lacks it.
  return {RefreshVerb::Buffer, RefreshWave::Fast};
}
