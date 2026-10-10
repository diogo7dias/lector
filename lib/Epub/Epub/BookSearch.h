#pragma once
#include <cstdint>
#include <string>

class Epub;
class SearchMatcher;

namespace book_search {

// Called once per hit: the hit's visible-codepoint offset in the chapter (what
// Section::getPageForVisibleTextOffset takes) and a snippet around it.
using HitFn = void (*)(void* ctx, uint32_t offset, const std::string& snippet);

// Streams one spine item out of the EPUB and runs `matcher` over its visible text, counting
// offsets exactly as ChapterHtmlSlimParser does, so a hit lands on the page that shows it.
// Nothing is laid out and nothing is written to the card. False when the item cannot be read.
bool searchSpineItem(const Epub& epub, int spineIndex, SearchMatcher& matcher, HitFn onHit, void* ctx);

}  // namespace book_search
