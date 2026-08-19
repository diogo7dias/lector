#pragma once
#include <string>

#include "activities/Activity.h"

class Bitmap;
class HalFile;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout) {}
  void onEnter() override;

 private:
  // Everything onEnter() does apart from bookkeeping. Split out so onEnter() can wrap it
  // and record which wallpaper (if any) ended up on the panel; the render functions
  // return from several places.
  void renderSleepScreen() const;
  // Blank FULL pass before painting a sleep face, so the screen the user locked from
  // does not ghost through it. See the definition for why nothing else provides it.
  // Keeps the page already on the panel and adds a thin border. Must not be preceded
  // by the popup or the deep clean.
  void deepCleanPanel() const;
  // darkBackground: the crest screen normally inverts, which is what Dark (and every
  // mode that falls back to it) asks for. The transparent-overlay fallback passes false:
  // that mode never asked for a dark screen, it asked for a picture over the page.
  // The polarity the sleep-screen setting implies, for the callers that fall back to the
  // crest screen because their own artwork was missing.
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  // preserveBackground keeps whatever the panel is already holding and draws the bitmap
  // over it: no initial clear, and no cover filter (the filter is about how a full-screen
  // wallpaper looks, and inverting a composite would invert the retained page too).
  void renderBitmapSleepScreen(const Bitmap& bitmap, bool preserveBackground = false) const;
  // Alpha overlay over the retained page. Reads a 32-bit BGRA BMP and composites it
  // with a Bayer dither on the alpha channel, so partial transparency survives a
  // 1-bit panel. Falls back to a plain over-draw for any other BMP.
  bool renderSleepOverlayFile(HalFile& file, const char* pathForLog) const;
  // PNG overlays go through the shared EPUB image decoder instead, which already
  // handles every PNG colour type; it grew a preserve-alpha mode for this.
  bool renderTransparentOverlayPng(const std::string& path) const;
  // Dispatches one overlay path to the PNG or the BMP renderer by extension.
  bool renderSleepOverlayPath(const std::string& path) const;
  void renderTransparentCustomSleepScreen() const;
  // Reading-stats dashboard over the current book's cover. Falls back to the default
  // face when there is no open book, the format has no stats, or no cover can be made.
  void renderStatsDashboardSleepScreen() const;
  void renderLastScreenSleepScreen() const;
  void renderBlankSleepScreen() const;

  bool fromTimeout = false;
  // The wallpaper the previous sleep left on the panel. onEnter clears the shared
  // APP_STATE field before rendering (so a non-wallpaper face leaves it empty),
  // and the paused-rotation path needs the old value to know what to hold.
  mutable std::string previousWallpaper;
  // True when the render path changed APP_STATE beyond lastSleepWallpaperPath
  // (rotation cursor advanced, index flagged stale). onEnter folds it into the
  // single batched saveToFile before deep sleep.
  mutable bool stateDirty = false;
};
