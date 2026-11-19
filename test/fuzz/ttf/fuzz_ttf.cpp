// Fuzzes the SD-card TTF/OTF load path exactly as TtfSdFont::loadFace and onGlyphMiss run it:
// size check, readTtfMetrics, TtfFont::init in the 32 KB arena, the ligature probe, then
// rasterize + advance + pack2Bit for a spread of codepoints at the metric-derived pixel height.
//
// Input: 1 byte, then the font file. The byte's low 7 bits are the point size (clamped to 8..40
// like clampTtfPointSize); bit 7 set skips checkTtfFace, leaving only the sfnt metrics read in
// front of stb, so the rasterizer's own bounds are tested without that pre-check's help.
// The font bytes sit in an exact-size heap block so ASan sees every read past the end, which
// the device's PSRAM block would only turn into a silent wrong read.
#include <StbTtfPrelude.h>
#include <TtfFamilyScan.h>
#include <render/TtfFont.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace {
size_t gLive = 0;
size_t gPeakLive = 0;
size_t gPeakRequest = 0;
// What stb may hold at once for one glyph before the harness calls it unbounded. The X4 Pro has
// 8 MB of PSRAM shared with the font files and their glyph caches.
constexpr size_t kMaxLive = 2 * STB_TTF_MAX_ALLOC;
}  // namespace

// The firmware's lectorStbMalloc (TtfFontImpl.cpp) caps one request; this one also counts, and
// enforces a cap on what is live at once so a glyph that makes stb allocate without bound fails
// here as an out-of-memory instead of passing.
void* lectorStbMalloc(const size_t size) {
  if (size > STB_TTF_MAX_ALLOC || gLive + size > kMaxLive) return nullptr;
  auto* p = static_cast<size_t*>(malloc(size + 16));
  if (p == nullptr) return nullptr;
  *p = size;
  gLive += size;
  if (gLive > gPeakLive) gPeakLive = gLive;
  if (size > gPeakRequest) gPeakRequest = size;
  return reinterpret_cast<uint8_t*>(p) + 16;
}

void lectorStbFree(void* ptr) {
  if (ptr == nullptr) return;
  auto* p = reinterpret_cast<size_t*>(static_cast<uint8_t*>(ptr) - 16);
  gLive -= *p;
  free(p);
}

namespace {
// FUZZ_TTF_STATS=1 prints the largest single stb request and the most it held at once.
struct StatsAtExit {
  ~StatsAtExit() {
    if (getenv("FUZZ_TTF_STATS") != nullptr) {
      fprintf(stderr, "stb scratch: largest request %zu bytes, peak live %zu bytes\n", gPeakRequest, gPeakLive);
    }
  }
} statsAtExit;

constexpr size_t TTF_ARENA_BYTES = 32 * 1024;
constexpr uint32_t kProbe[] = {0x20,   0x41,   0x61,   0x67,   0x7E,   0xA0,   0xC9,   0x151,  0x20AC,  0x2026,
                               0xFB00, 0xFB01, 0xFB02, 0xFB03, 0xFB04, 0xFFFD, 0x4E00, 0xFFFF, 0x10FFFF};
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, const size_t size) {
  if (size < 1 + 12) return 0;
  const bool strict = (data[0] & 0x80) == 0;
  const uint8_t pointSize = ttfscan::clampTtfPointSize(data[0] & 0x7F);
  const size_t len = size - 1;
  if (len > ttfscan::TTF_MAX_FACE_BYTES) return 0;

  std::unique_ptr<uint8_t[]> bytes(new uint8_t[len]);
  memcpy(bytes.get(), data + 1, len);

  ttfscan::TtfMetrics metrics;
  if (strict ? ttfscan::checkTtfFace(bytes.get(), len, metrics) != ttfscan::TtfReject::None
             : !ttfscan::readTtfMetrics(bytes.get(), len, metrics)) {
    return 0;
  }

  std::unique_ptr<uint8_t[]> arenaBytes(new uint8_t[TTF_ARENA_BYTES]);
  freeink::book::Arena arena;
  arena.init(arenaBytes.get(), TTF_ARENA_BYTES);
  freeink::book::TtfFont ttf;
  if (!ttf.init(bytes.get(), static_cast<uint32_t>(len), arena)) return 0;

  const uint16_t emPx = ttfscan::ttfEmPixels(pointSize);
  const uint16_t sizePx = ttfscan::ttfHeightForEm(metrics, emPx);
  (void)ttfscan::ttfScaleUnits(metrics, static_cast<int32_t>(metrics.ascender) - metrics.descender + metrics.lineGap,
                               emPx);

  // Codepoints: the fixed probe set, plus 8 taken from the font bytes themselves so the
  // fuzzer can steer into cmap ranges the probe misses.
  std::vector<uint32_t> cps(std::begin(kProbe), std::end(kProbe));
  for (size_t i = 0; i < 8 && i * 3 + 3 <= len; ++i) {
    const size_t at = (len / 9) * i;
    if (at + 3 <= len) cps.push_back((bytes[at] << 16) | (bytes[at + 1] << 8) | bytes[at + 2]);
  }

  for (const uint32_t cp : cps) {
    ttf.hasGlyph(cp);
    const freeink::book::GlyphBitmap* g = ttf.rasterize(cp, sizePx);
    if (g == nullptr) continue;
    if (g->width > 255 || g->height > 255) continue;
    const size_t packed = ttfscan::packed2BitBytes(g->width, g->height);
    std::unique_ptr<uint8_t[]> out(new uint8_t[packed > 0 ? packed : 1]);
    if (g->pixels != nullptr) ttfscan::pack2Bit(g->pixels, g->width, g->height, out.get());
    (void)ttf.advance(cp, static_cast<uint16_t>(sizePx * 64), 0);
  }
  // Every scratch block stb took for these glyphs has to be back, whichever way it failed.
  if (gLive != 0) {
    fprintf(stderr, "stb leaked %zu bytes of scratch\n", gLive);
    abort();
  }
  return 0;
}
