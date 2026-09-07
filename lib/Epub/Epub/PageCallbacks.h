#pragma once

#include <cstdint>
#include <memory>

class Page;

// The two callbacks a chapter build takes. Function pointer plus caller context rather
// than std::function: a build runs on the OOM-sensitive render path, where each
// std::function costs a heap-allocated closure and its own copy of the call machinery in
// flash. Shared by ChapterHtmlSlimParser, which calls them, and Section, which forwards
// them from the reader.

// Called with each finished page. `ctx` is whatever the caller handed over.
using PageCompleteFn = void (*)(void* ctx, std::unique_ptr<Page> page, uint16_t paragraphIndex, uint16_t listItemIndex,
                                uint32_t visibleTextOffset);

// Called when a build is slow enough to deserve the indexing popup.
using PopupFn = void (*)(void* ctx);
