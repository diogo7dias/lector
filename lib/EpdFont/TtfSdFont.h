#pragma once

// SD-card TTF/OTF family presented as EpdFont faces, so the existing layout,
// section cache and GfxRenderer text path draw it unchanged (PSRAM-class
// builds only; see CROSSPOINT_TTF_READER in platformio.ini).
//
// Each face keeps its file bytes, the stb_truetype rasterizer (the FreeInk
// SDK's TtfFont) and a glyph cache in PSRAM. Glyphs are rasterized on first
// use through EpdFontData::glyphMissHandler, quantised to the 2-bit format the
// renderer already blits, and kept until the cache fills, when the whole
// cache flushes (direct-mapped, no LRU bookkeeping). Nothing here touches
// DRAM beyond the object itself.
//
// The caller (SdCardFontManager) registers the four faces as one
// EpdFontFamily under a font id derived from the file bytes, family name and
// point size (TtfFamilyScan.h), which keys the section cache exactly like a
// .cpfont family.

#ifdef CROSSPOINT_TTF_READER

#include <BookArena.h>
#include <render/TtfFont.h>

#include <cstdint>
#include <memory>

#include "EpdFont.h"
#include "EpdFontData.h"

struct SdCardFontFamilyInfo;

class TtfSdFont {
 public:
  static constexpr uint8_t MAX_STYLES = 4;

  TtfSdFont();
  ~TtfSdFont();
  TtfSdFont(const TtfSdFont&) = delete;
  TtfSdFont& operator=(const TtfSdFont&) = delete;

  // Loads every face the family lists (TtfFamilyScan rules) at `pointSize`.
  // Fails when the regular face cannot be loaded; other faces degrade to
  // absent (EpdFontFamily then falls back to regular).
  bool load(const SdCardFontFamilyInfo& family, uint8_t pointSize);

  // Face for an EpdFontFamily style index, nullptr when the family has none.
  EpdFont* getEpdFont(uint8_t style);

  // FNV-1a over the bytes of every loaded face: part of the font id.
  uint32_t contentHash() const { return contentHash_; }
  uint8_t pointSize() const { return pointSize_; }

  // GfxRenderer::getGlyphBitmap(): true when `ctx` is one of this font's miss
  // contexts, in which case the glyph bitmap sits at
  // fontData->bitmap + glyph->dataOffset like a flash font's.
  static bool ownsMissCtx(const void* ctx);

 private:
  struct Slot {
    uint32_t cp;
    EpdGlyph glyph;
  };
  struct Face;

  struct MissCtx {
    TtfSdFont* self;
    uint8_t style;
  };

  static const EpdGlyph* onGlyphMiss(void* ctx, uint32_t codepoint);
  static bool onCoverageQuery(void* ctx, uint32_t codepoint);
  bool loadFace(uint8_t style, const char* path, uint8_t pointSize);

  std::unique_ptr<Face> faces_[MAX_STYLES];
  MissCtx ctx_[MAX_STYLES];
  uint32_t contentHash_ = 0;
  uint8_t pointSize_ = 0;

  // One TTF family is resident at a time (SdCardFontManager), so the bitmap
  // lookup can identify a miss context by comparing against this instance.
  static TtfSdFont* active_;
};

#endif  // CROSSPOINT_TTF_READER
