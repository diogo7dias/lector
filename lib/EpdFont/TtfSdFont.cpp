#ifdef CROSSPOINT_TTF_READER

#include "TtfSdFont.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_heap_caps.h>

#include <cstring>

#include "SdCardFontRegistry.h"
#include "TtfFamilyScan.h"

namespace {

constexpr const char* TAG = "TTF";

// PSRAM-only buffers: font bytes and glyph caches never touch DRAM. A failed
// PSRAM allocation is a load failure, not a DRAM fallback.
struct PsramFree {
  void operator()(uint8_t* p) const { heap_caps_free(p); }
};
using PsramBytes = std::unique_ptr<uint8_t[], PsramFree>;

PsramBytes psramAlloc(const size_t bytes) {
  return PsramBytes(static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

// TtfFont's own arena: its slot tables (~12 KB at the standard profile) plus
// 8-bit scratch bitmaps. The 2-bit copy below is what the renderer reads, so
// this only has to fit a few glyphs between flushes.
constexpr size_t TTF_ARENA_BYTES = 32 * 1024;
// 2-bit glyph cache per face. ~90 KB holds a full Latin repertoire at 40 pt
// (83 px em, ~1.2 KB per glyph) or thousands of body-size glyphs.
constexpr size_t GLYPH_ARENA_BYTES = 96 * 1024;
constexpr uint32_t SLOT_COUNT = 512;
constexpr uint32_t NO_CP = 0xFFFFFFFFu;
constexpr size_t READ_CHUNK = 16 * 1024;

// Standard Latin ligatures TtfFont can substitute, sorted by packed pair for
// EpdFont's binary search.
constexpr uint32_t kLigaturePairs[][2] = {
    {('f' << 16) | 'f', 0xFB00},     {('f' << 16) | 'i', 0xFB01},     {('f' << 16) | 'l', 0xFB02},
    {(0xFB00u << 16) | 'i', 0xFB03}, {(0xFB00u << 16) | 'l', 0xFB04},
};

}  // namespace

struct TtfSdFont::Face {
  freeink::book::TtfFont ttf;
  freeink::book::Arena arena;
  PsramBytes fileBytes;
  PsramBytes arenaBytes;
  PsramBytes slotBytes;
  PsramBytes glyphBytes;
  uint32_t fileLen = 0;
  Slot* slots = nullptr;
  size_t glyphUsed = 0;
  uint16_t sizePx = 0;  // TtfFont pixel height that makes the em the wanted size
  EpdLigaturePair ligatures[5] = {};
  EpdFontData data = {};
  EpdFont font;

  Face() : font(&data) {}

  void resetSlots() {
    for (uint32_t i = 0; i < SLOT_COUNT; ++i) slots[i].cp = NO_CP;
    glyphUsed = 0;
  }
};

TtfSdFont* TtfSdFont::active_ = nullptr;

TtfSdFont::TtfSdFont() {
  for (uint8_t i = 0; i < MAX_STYLES; ++i) ctx_[i] = {this, i};
  if (active_ == nullptr) active_ = this;
}

TtfSdFont::~TtfSdFont() {
  if (active_ == this) active_ = nullptr;
}

bool TtfSdFont::ownsMissCtx(const void* ctx) {
  if (active_ == nullptr || ctx == nullptr) return false;
  for (const MissCtx& c : active_->ctx_) {
    if (ctx == &c) return true;
  }
  return false;
}

EpdFont* TtfSdFont::getEpdFont(const uint8_t style) {
  return style < MAX_STYLES && faces_[style] ? &faces_[style]->font : nullptr;
}

bool TtfSdFont::load(const SdCardFontFamilyInfo& family, const uint8_t pointSize) {
  if (active_ != this) {
    LOG_ERR(TAG, "Another TTF family is still resident");
    return false;
  }
  pointSize_ = ttfscan::clampTtfPointSize(pointSize);
  contentHash_ = ttfscan::FNV1A_SEED;
  for (uint8_t style = 0; style < MAX_STYLES; ++style) {
    faces_[style].reset();
    const std::string& path = family.ttfFaces[style];
    if (path.empty()) continue;
    if (!loadFace(style, path.c_str(), pointSize_)) {
      LOG_ERR(TAG, "Face %u of %s failed to load: %s", style, family.name.c_str(), path.c_str());
      faces_[style].reset();
    }
  }
  if (!faces_[0]) {
    LOG_ERR(TAG, "%s has no usable regular face", family.name.c_str());
    return false;
  }
  return true;
}

bool TtfSdFont::loadFace(const uint8_t style, const char* path, const uint8_t pointSize) {
  auto face = makeUniqueNoThrow<Face>();
  if (!face) {
    LOG_ERR(TAG, "OOM: face object");
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead(TAG, path, file)) return false;
  const size_t len = file.fileSize();
  if (len < 12 || len > ttfscan::TTF_MAX_FACE_BYTES) {
    LOG_ERR(TAG, "%s: %u bytes, limit %u", path, static_cast<unsigned>(len),
            static_cast<unsigned>(ttfscan::TTF_MAX_FACE_BYTES));
    return false;
  }
  face->fileBytes = psramAlloc(len);
  face->arenaBytes = psramAlloc(TTF_ARENA_BYTES);
  face->slotBytes = psramAlloc(SLOT_COUNT * sizeof(Slot));
  face->glyphBytes = psramAlloc(GLYPH_ARENA_BYTES);
  if (!face->fileBytes || !face->arenaBytes || !face->slotBytes || !face->glyphBytes) {
    LOG_ERR(TAG, "OOM (PSRAM): %u bytes for %s", static_cast<unsigned>(len), path);
    return false;
  }
  for (size_t off = 0; off < len;) {
    const size_t want = len - off < READ_CHUNK ? len - off : READ_CHUNK;
    const int got = file.read(face->fileBytes.get() + off, want);
    if (got <= 0) {
      LOG_ERR(TAG, "Short read at %u of %s", static_cast<unsigned>(off), path);
      return false;
    }
    off += static_cast<size_t>(got);
  }
  face->fileLen = static_cast<uint32_t>(len);

  ttfscan::TtfMetrics metrics;
  if (!ttfscan::readTtfMetrics(face->fileBytes.get(), len, metrics)) {
    LOG_ERR(TAG, "Not a usable sfnt: %s", path);
    return false;
  }
  face->arena.init(face->arenaBytes.get(), TTF_ARENA_BYTES);
  if (!face->ttf.init(face->fileBytes.get(), face->fileLen, face->arena)) {
    LOG_ERR(TAG, "stb_truetype rejected %s", path);
    return false;
  }

  const uint16_t emPx = ttfscan::ttfEmPixels(pointSize);
  face->sizePx = ttfscan::ttfHeightForEm(metrics, emPx);
  face->slots = reinterpret_cast<Slot*>(face->slotBytes.get());
  face->resetSlots();

  EpdFontData& d = face->data;
  d.bitmap = face->glyphBytes.get();
  d.glyph = nullptr;
  d.intervals = nullptr;
  d.intervalCount = 0;
  d.advanceY = static_cast<uint8_t>(ttfscan::ttfScaleUnits(
      metrics, static_cast<int32_t>(metrics.ascender) - metrics.descender + metrics.lineGap, emPx));
  d.ascender = ttfscan::ttfScaleUnits(metrics, metrics.ascender, emPx);
  d.descender = ttfscan::ttfScaleUnits(metrics, metrics.descender, emPx);
  d.is2Bit = true;
  uint32_t ligCount = 0;
  for (const auto& pair : kLigaturePairs) {
    if (face->ttf.hasGlyph(pair[1])) face->ligatures[ligCount++] = {pair[0], pair[1]};
  }
  d.ligaturePairs = ligCount > 0 ? face->ligatures : nullptr;
  d.ligaturePairCount = ligCount;
  d.glyphMissHandler = &TtfSdFont::onGlyphMiss;
  d.glyphMissCtx = &ctx_[style];
  d.coverageHandler = &TtfSdFont::onCoverageQuery;

  contentHash_ = ttfscan::fnv1a(contentHash_, face->fileBytes.get(), len);
  LOG_DBG(TAG, "Loaded %s: %u bytes, em %u px, height %u px, line %u px, %u ligatures", path,
          static_cast<unsigned>(len), emPx, face->sizePx, d.advanceY, static_cast<unsigned>(ligCount));
  faces_[style] = std::move(face);
  return true;
}

const EpdGlyph* TtfSdFont::onGlyphMiss(void* ctx, const uint32_t codepoint) {
  auto* mc = static_cast<MissCtx*>(ctx);
  Face* face = mc->self->faces_[mc->style].get();
  if (face == nullptr) return nullptr;

  // ponytail: direct-mapped cache, collisions overwrite. Returned pointers stay
  // valid until another codepoint lands in the same slot; callers consume a
  // glyph (measure or draw) before asking for the next, as with SdCardFont.
  Slot& slot = face->slots[codepoint % SLOT_COUNT];
  if (slot.cp == codepoint) return &slot.glyph;

  const freeink::book::GlyphBitmap* g = face->ttf.rasterize(codepoint, face->sizePx);
  if (g == nullptr) return nullptr;                       // not in this face: EpdFont falls back to U+FFFD
  if (g->width > 255 || g->height > 255) return nullptr;  // EpdGlyph carries 8-bit extents

  const size_t bytes = ttfscan::packed2BitBytes(g->width, g->height);
  if (face->glyphUsed + bytes > GLYPH_ARENA_BYTES) {
    face->resetSlots();  // cache full: start a fresh generation
    if (bytes > GLYPH_ARENA_BYTES) return nullptr;
  }
  uint8_t* out = face->glyphBytes.get() + face->glyphUsed;
  ttfscan::pack2Bit(g->pixels, g->width, g->height, out);

  slot.cp = codepoint;
  EpdGlyph& e = slot.glyph;
  e.width = static_cast<uint8_t>(g->width);
  e.height = static_cast<uint8_t>(g->height);
  // 12.4 fixed-point advance: TtfFont scales linearly with the pixel height,
  // so asking for 16x the height yields the advance in sixteenths of a pixel.
  e.advanceX = static_cast<uint16_t>(face->ttf.advance(codepoint, static_cast<uint16_t>(face->sizePx * 16), 0));
  e.left = g->xoff;
  e.top = static_cast<int16_t>(-g->yoff);
  e.dataLength = static_cast<uint16_t>(bytes);
  e.dataOffset = static_cast<uint32_t>(face->glyphUsed);
  face->glyphUsed += bytes;
  return &e;
}

bool TtfSdFont::onCoverageQuery(void* ctx, const uint32_t codepoint) {
  auto* mc = static_cast<MissCtx*>(ctx);
  const Face* face = mc->self->faces_[mc->style].get();
  return face != nullptr && face->ttf.hasGlyph(codepoint);
}

#endif  // CROSSPOINT_TTF_READER
