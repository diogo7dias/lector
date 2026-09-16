#include "SdCardFontManager.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <SdCardFont.h>
#include <SdCardFontRegistry.h>
#include <TtfFamilyScan.h>
#ifdef CROSSPOINT_TTF_READER
#include <Memory.h>
#include <TtfSdFont.h>
#endif

SdCardFontManager::~SdCardFontManager() {
  for (auto& lf : loaded_) {
    delete lf.font;
  }
#ifdef CROSSPOINT_TTF_READER
  delete ttf_;
#endif
}

#ifdef CROSSPOINT_TTF_READER
int SdCardFontManager::loadTtfFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer,
                                     const uint8_t pointSize) {
  auto font = makeUniqueNoThrow<TtfSdFont>();
  if (!font) {
    LOG_ERR("SDMGR", "OOM: TtfSdFont for %s", family.name.c_str());
    return 0;
  }
  if (!font->load(family, pointSize)) return 0;

  const int fontId = ttfscan::fontIdForFamilySize(font->contentHash(), family.name.c_str(), font->pointSize());
  if (renderer.getFontMap().count(fontId) != 0) {
    LOG_ERR("SDMGR", "Font ID %d collides with existing font, skipping %s", fontId, family.name.c_str());
    return 0;
  }
  EpdFontFamily fontFamily(font->getEpdFont(0), font->getEpdFont(1), font->getEpdFont(2), font->getEpdFont(3));
  renderer.insertFont(fontId, fontFamily);
  ttf_ = font.release();
  ttfFontId_ = fontId;
  LOG_DBG("SDMGR", "Loaded TTF family %s at %u pt id=%d", family.name.c_str(), ttf_->pointSize(), fontId);
  return fontId;
}
#endif

int SdCardFontManager::loadFile(const SdCardFontFileInfo& file, const char* familyName, GfxRenderer& renderer) {
  auto* font = new (std::nothrow) SdCardFont();
  if (!font) {
    LOG_ERR("SDMGR", "Failed to allocate SdCardFont for %s", file.path.c_str());
    return 0;
  }

  if (!font->load(file.path.c_str())) {
    LOG_ERR("SDMGR", "Failed to load %s", file.path.c_str());
    delete font;
    return 0;
  }

  // Deterministic id, stable across load/unload cycles and reboots, changing when
  // font content changes (different header/TOC = different contentHash).
  int fontId = ttfscan::fontIdForFamilySize(font->contentHash(), familyName, file.pointSize);
  // Guard against collision with built-in font IDs (astronomically unlikely
  // with FNV-1a hashes, but provides a safety net)
  if (renderer.getFontMap().count(fontId) != 0) {
    LOG_ERR("SDMGR", "Font ID %d collides with existing font, skipping %s", fontId, file.path.c_str());
    delete font;
    return 0;
  }
  renderer.registerSdCardFont(fontId, font);
  loaded_.push_back({font, fontId, file.pointSize});

  LOG_DBG("SDMGR", "Loaded %s size=%u id=%d styles=%u", file.path.c_str(), file.pointSize, fontId, font->styleCount());

  EpdFontFamily fontFamily(font->getEpdFont(0), font->getEpdFont(1), font->getEpdFont(2), font->getEpdFont(3));
  renderer.insertFont(fontId, fontFamily);
  return fontId;
}

bool SdCardFontManager::loadFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t pointSize) {
  // Unload any previously loaded family first
  if (!loadedFamilyName_.empty()) {
    unloadAll(renderer);
  }

#ifdef CROSSPOINT_TTF_READER
  if (family.hasTtf()) {
    if (loadTtfFamily(family, renderer, pointSize) == 0) return false;
    loadedFamilyName_ = family.name;
    loadedPointSize_ = ttf_->pointSize();
    return true;
  }
#endif

  const SdCardFontFileInfo* selected = family.findNearestSize(pointSize);
  if (!selected) {
    LOG_ERR("SDMGR", "Family %s has no files to load", family.name.c_str());
    return false;
  }

  if (loadFile(*selected, family.name.c_str(), renderer) == 0) {
    return false;
  }

  loadedFamilyName_ = family.name;
  loadedPointSize_ = selected->pointSize;
  return true;
}

int SdCardFontManager::loadFamilyExtraSize(const SdCardFontFamilyInfo& family, GfxRenderer& renderer,
                                           uint8_t pointSize) {
  if (family.hasTtf()) return 0;  // see loadTtfFamily()
  const SdCardFontFileInfo* file = family.findFile(pointSize);
  if (!file) return 0;  // family has no .cpfont at this exact size

  // Reuse an already-loaded font of the same size (e.g. when a reader size
  // happens to match a UI size) instead of double-loading the file.
  for (const auto& lf : loaded_) {
    if (lf.size == pointSize) return lf.fontId;
  }

  return loadFile(*file, family.name.c_str(), renderer);
}

void SdCardFontManager::unloadAll(GfxRenderer& renderer) {
  // Drop UI CJK fallbacks before the SD fonts they point at are freed.
  renderer.clearFallbackFonts();
  renderer.clearSdCardFonts();
  for (auto& lf : loaded_) {
    renderer.removeFont(lf.fontId);
    delete lf.font;
  }
  loaded_.clear();
#ifdef CROSSPOINT_TTF_READER
  if (ttfFontId_ != 0) renderer.removeFont(ttfFontId_);
  delete ttf_;
  ttf_ = nullptr;
  ttfFontId_ = 0;
#endif
  loadedFamilyName_.clear();
  loadedPointSize_ = 0;
}

int SdCardFontManager::getFontId(const std::string& familyName) const {
#ifdef CROSSPOINT_TTF_READER
  if (ttfFontId_ != 0 && familyName == loadedFamilyName_) return ttfFontId_;
#endif
  if (familyName != loadedFamilyName_ || loaded_.empty()) return 0;
  return loaded_.front().fontId;
}
