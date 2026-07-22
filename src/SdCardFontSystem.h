#pragma once

#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>

#include <atomic>

class GfxRenderer;

/// Facade that owns the SD card font registry, manager, and resolver logic.
/// Hides implementation details behind a single begin() + ensureLoaded() API.
class SdCardFontSystem {
 public:
  SdCardFontSystem() = default;
  SdCardFontSystem(const SdCardFontSystem&) = delete;
  SdCardFontSystem& operator=(const SdCardFontSystem&) = delete;
  /// Register the SD font resolver in settings. Call once during setup.
  /// The SD /fonts directory scan and the user's saved-family load are
  /// deferred to the first ensureLoaded()/refreshIfDirty() so plain boots
  /// (built-in fonts) never pay the scan before first paint.
  void begin(GfxRenderer& renderer);

  /// Ensure the correct SD font family is loaded for the current settings.
  /// Call before entering the reader or after settings change.
  /// Also re-discovers if the registry has been marked dirty (e.g. by web upload).
  void ensureLoaded(GfxRenderer& renderer);

  /// Ensure a SPECIFIC family + size is loaded, independent of the global
  /// SETTINGS reader fields. The reader uses this for per-book overrides
  /// (prefs_), whose SD font family / size can differ from the global settings
  /// the manager was last synced to. Without it, resolveFontId() returns the
  /// globally-loaded font's id (or 0 -> built-in fallback) and an in-book font
  /// or size change never takes effect. Unlike ensureLoaded(), a missing /
  /// unloadable font here NEVER clears the global SETTINGS.sdFontFamilyName —
  /// a per-book miss must not wipe the user's global font. `family` may be null
  /// or empty to select "no SD font" (built-ins).
  void ensureLoadedFor(GfxRenderer& renderer, const char* family, uint8_t sizeEnum);

  /// Resolve an SD card font ID from family name + fontSize enum.
  /// Returns 0 if not found. Used by CrossPointSettings::getReaderFontId().
  int resolveFontId(const char* familyName, uint8_t fontSizeEnum) const;

  /// Access the registry (e.g. for settings UI to enumerate available fonts).
  const SdCardFontRegistry& registry() const { return registry_; }

  /// Non-const access to the registry (for FontInstaller).
  SdCardFontRegistry& registry() { return registry_; }

  /// Mark the registry as needing re-discovery.
  /// Thread-safe: can be called from the web server task.
  void markRegistryDirty() {
    registryDirty_.store(true, std::memory_order_release);
    reloadDirty_.store(true, std::memory_order_release);
  }

  /// If the registry is dirty, re-scan the SD card now and clear the flag.
  /// Used by the web UI so uploaded/deleted fonts appear in the list
  /// without waiting for the reader activity to run ensureLoaded().
  void refreshIfDirty() {
    const bool dirty = registryDirty_.exchange(false, std::memory_order_acquire);
    const bool firstUse = !discovered_.exchange(true, std::memory_order_acq_rel);
    if (dirty || firstUse) {
      registry_.discover();
    }
  }

 private:
  SdCardFontRegistry registry_;
  SdCardFontManager manager_;
  // False until the first /fonts scan has run (deferred out of begin()).
  // Atomic because the web server task can race the main task here.
  std::atomic<bool> discovered_{false};
  std::atomic<bool> registryDirty_{false};
  // Kept separate because the web list may consume the registry rescan while
  // the reader still must reload an active file that was replaced in place.
  std::atomic<bool> reloadDirty_{false};
};

// Global SD card font system instance (defined in main.cpp).
extern SdCardFontSystem sdFontSystem;
