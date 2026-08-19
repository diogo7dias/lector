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
  /// Register the font-id resolver and load the user's saved SD selection. Call once during
  /// setup. The card is only scanned here when a custom font is actually selected; with the
  /// built-in fonts the scan is deferred to the first caller that needs the family list.
  void begin(GfxRenderer& renderer);

  /// Ensure the correct SD font family is loaded for the current global settings.
  /// Call before entering the reader or after settings change.
  /// Also re-discovers if the registry has been marked dirty (e.g. by web upload).
  void ensureLoaded(GfxRenderer& renderer);

  /// Per-book variant: make the resident SD font match one book's ReaderPrefs rather
  /// than the global reader selection. Only one SD size is resident at a time and
  /// resolveFontId() returns whichever that is, so a book with a per-book font family
  /// or size must call this before it lays out, or it silently gets the global one.
  /// Unlike ensureLoaded(), a missing family here never clears the global selection —
  /// the book simply falls back to the built-in font, and no snap is persisted.
  void ensureLoadedFor(GfxRenderer& renderer, const char* familyName, uint8_t pointSize);

  /// Resolve an SD card font ID from family name + reader point size.
  /// Returns 0 if not found. Used by CrossPointSettings::getReaderFontId().
  int resolveFontId(const char* familyName, uint8_t pointSize) const;

  /// Access the registry (e.g. for settings UI to enumerate available fonts).
  /// Scans the card on first use if begin() deferred it.
  const SdCardFontRegistry& registry() const {
    ensureDiscovered();
    return registry_;
  }

  /// Non-const access to the registry (for FontInstaller).
  SdCardFontRegistry& registry() {
    ensureDiscovered();
    return registry_;
  }

  /// Mark the registry as needing re-discovery.
  /// Thread-safe: can be called from the web server task.
  void markRegistryDirty() { registryDirty_.store(true, std::memory_order_release); }

  /// If the registry is dirty, re-scan the SD card now and clear the flag.
  /// Used by the web UI so uploaded/deleted fonts appear in the list
  /// without waiting for the reader activity to run ensureLoaded().
  void refreshIfDirty() {
    if (registryDirty_.exchange(false, std::memory_order_acquire)) {
      registry_.discover();
      discovered_ = true;
    }
  }

 private:
  // Load the active SD family at the built-in UI point sizes and register each
  // as a size-matched CJK fallback for the corresponding UI font, so CJK book
  // titles/list rows render at the same size as the surrounding Latin UI text.
  // No-op when no SD family is loaded. Safe to call repeatedly (sizes already
  // loaded are reused).
  void setupUiFallbacks(GfxRenderer& renderer);

  // Shared body of ensureLoaded()/ensureLoadedFor(). ownsGlobalSelection tells it
  // whether wantedFamily IS the global selection, and so whether a family that has
  // gone missing should clear SETTINGS.sdFontFamilyName. A book's own family must
  // never do that.
  void ensureLoadedImpl(GfxRenderer& renderer, const char* wantedFamily, uint8_t pointSize, bool ownsGlobalSelection);

  // Scan the card once, on demand. Booting with the built-in fonts never needs the family
  // list, and the scan walks both font roots probing every file, which is pure startup cost
  // for a reader that has no SD fonts installed. mutable so the const registry() accessor can
  // still trigger the deferred scan.
  void ensureDiscovered() const;

  mutable SdCardFontRegistry registry_;
  mutable bool discovered_ = false;
  SdCardFontManager manager_;
  std::atomic<bool> registryDirty_{false};
};

// Global SD card font system instance (defined in main.cpp).
extern SdCardFontSystem sdFontSystem;
