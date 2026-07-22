#include "SdCardFontSystem.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include "CrossPointSettings.h"

namespace {

static uint8_t fontSizeEnumFromSettings() {
  uint8_t e = SETTINGS.fontSize;
  if (e >= CrossPointSettings::FONT_SIZE_COUNT) e = 1;  // default to MEDIUM
  return e;
}

}  // namespace

void SdCardFontSystem::begin(GfxRenderer& /*renderer*/) {
  // Register this system as the SD font ID resolver in settings.
  // Uses a static trampoline since CrossPointSettings stores a plain function pointer.
  SETTINGS.sdFontIdResolver = [](void* ctx, const char* familyName, uint8_t fontSizeEnum) -> int {
    return static_cast<SdCardFontSystem*>(ctx)->resolveFontId(familyName, fontSizeEnum);
  };
  SETTINGS.sdFontResolverCtx = this;

  // The /fonts scan and the saved-family load are deferred: every consumer of
  // SD fonts (reader entry, font picker, web font list) already calls
  // ensureLoaded()/refreshIfDirty() first, and those run the first scan.
  LOG_DBG("SDFS", "SD font resolver registered (scan deferred to first use)");
}

void SdCardFontSystem::ensureLoaded(GfxRenderer& renderer) {
  // If the web server (or another task) installed/deleted fonts, re-discover.
  // Track whether we just re-discovered so we can force a reload below even
  // when the wanted family/size still maps to the same point size — the file
  // contents on disk may have changed (e.g. user re-uploaded a new build).
  const bool registryWasDirty = registryDirty_.exchange(false, std::memory_order_acquire);
  const bool reloadWasDirty = reloadDirty_.exchange(false, std::memory_order_acquire);
  const bool firstUse = !discovered_.exchange(true, std::memory_order_acq_rel);
  if (registryWasDirty || firstUse) {
    LOG_DBG("SDFS", "%s — discovering fonts", firstUse ? "First use" : "Registry dirty");
    registry_.discover();
  }

  const char* wantedFamily = SETTINGS.sdFontFamilyName;
  const std::string& currentFamily = manager_.currentFamilyName();
  const uint8_t sizeEnum = fontSizeEnumFromSettings();

  if (wantedFamily[0] == '\0') {
    if (!currentFamily.empty()) {
      manager_.unloadAll(renderer);
    }
    return;
  }

  // Reload if family changed OR if the user-selected size maps to a
  // different file than what's currently loaded OR if the registry was
  // just rediscovered (file may have been replaced on disk).
  bool familyMatches = (currentFamily == wantedFamily);
  if (familyMatches) {
    const auto* family = registry_.findFamily(wantedFamily);
    if (!family) {
      LOG_DBG("SDFS", "SD font family disappeared: %s (clearing)", wantedFamily);
      manager_.unloadAll(renderer);
      SETTINGS.sdFontFamilyName[0] = '\0';
      SETTINGS.saveToFile();
      return;
    }
    const auto* selected = family->findClosestReaderSize(sizeEnum);
    const uint8_t wantedPt = selected ? selected->pointSize : 0;
    if (!reloadWasDirty && wantedPt == manager_.currentPointSize()) return;
    LOG_DBG("SDFS", "Reloading %s: size %u -> %u (enum %u)%s", wantedFamily, manager_.currentPointSize(), wantedPt,
            sizeEnum, reloadWasDirty ? " [font changed]" : "");
  }

  if (!currentFamily.empty()) {
    manager_.unloadAll(renderer);
  }

  const auto* family = registry_.findFamily(wantedFamily);
  if (family) {
    if (manager_.loadFamily(*family, renderer, sizeEnum)) {
      LOG_DBG("SDFS", "Loaded SD font family: %s", wantedFamily);
    } else {
      LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", wantedFamily);
      SETTINGS.sdFontFamilyName[0] = '\0';
      SETTINGS.saveToFile();
    }
  } else {
    LOG_DBG("SDFS", "SD font family not found: %s (clearing)", wantedFamily);
    SETTINGS.sdFontFamilyName[0] = '\0';
    SETTINGS.saveToFile();
  }
}

void SdCardFontSystem::ensureLoadedFor(GfxRenderer& renderer, const char* family, uint8_t sizeEnum) {
  // Re-discover on first use or after a web-side install/delete, same as
  // ensureLoaded(), but drive the load from the explicit family + size instead
  // of the global SETTINGS fields, and never clear global settings on a miss.
  const bool registryWasDirty = registryDirty_.exchange(false, std::memory_order_acquire);
  const bool reloadWasDirty = reloadDirty_.exchange(false, std::memory_order_acquire);
  const bool firstUse = !discovered_.exchange(true, std::memory_order_acq_rel);
  if (registryWasDirty || firstUse) {
    registry_.discover();
  }

  if (sizeEnum >= CrossPointSettings::FONT_SIZE_COUNT) sizeEnum = 1;  // default MEDIUM

  // Empty family = built-in fonts: drop any loaded SD font so getFontId() -> 0.
  if (!family || family[0] == '\0') {
    if (!manager_.currentFamilyName().empty()) manager_.unloadAll(renderer);
    return;
  }

  const std::string wanted = family;
  const auto* fam = registry_.findFamily(wanted);
  if (!fam) {
    // Per-book font no longer on the card. Fall back to built-in for THIS book
    // (getFontId -> 0); do NOT touch the global setting.
    if (!manager_.currentFamilyName().empty()) manager_.unloadAll(renderer);
    return;
  }

  const auto* selected = fam->findClosestReaderSize(sizeEnum);
  const uint8_t wantedPt = selected ? selected->pointSize : 0;
  if (!reloadWasDirty && manager_.currentFamilyName() == wanted && wantedPt == manager_.currentPointSize()) {
    return;  // already loaded at the right size
  }

  if (!manager_.currentFamilyName().empty()) manager_.unloadAll(renderer);
  if (!manager_.loadFamily(*fam, renderer, sizeEnum)) {
    LOG_ERR("SDFS", "Failed to load per-book SD font: %s (falling back to built-in)", wanted.c_str());
  }
}

int SdCardFontSystem::resolveFontId(const char* familyName, uint8_t /*fontSizeEnum*/) const {
  // The manager loads exactly one size (closest to SETTINGS.fontSize), so the
  // enum is implicit — always return the single loaded font ID for this family.
  // ensureLoaded() must have been called with the current settings before this.
  return manager_.getFontId(familyName);
}
