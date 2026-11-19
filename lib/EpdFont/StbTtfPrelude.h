#pragma once

// Included ahead of stb_truetype.h in the TTF build (lib/EpdFont/TtfFontImpl.cpp, and the host fuzz
// target test/fuzz/ttf). stb_truetype says on its first lines that it must not be used on untrusted
// font files, and these fonts come off the SD card or the network, so the build narrows what a
// hostile file can do (the rest is the patch set in scripts/stb_truetype_patches):
//   - wrapping signed arithmetic: stb adds and subtracts offsets taken from the file, and with
//     overflow undefined the compiler may delete the range checks that follow;
//   - scratch allocations go through lectorStbMalloc, which refuses a request no real glyph needs.

#include <cstddef>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("wrapv")
#endif

// Largest single block stb may ask for: the outline of one glyph is a few KB, 1 MB is a font with
// tens of thousands of points in a single glyph.
constexpr size_t STB_TTF_MAX_ALLOC = 1u << 20;

void* lectorStbMalloc(size_t size);  // nullptr past STB_TTF_MAX_ALLOC, or when the heap is out
void lectorStbFree(void* ptr);

#define STBTT_malloc(x, u) ((void)(u), lectorStbMalloc(x))
#define STBTT_free(x, u) ((void)(u), lectorStbFree(x))
