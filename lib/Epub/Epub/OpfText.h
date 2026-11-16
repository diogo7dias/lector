#pragma once
#include <string>
#include <string_view>

namespace OpfText {

// dc:description is prose, but publishers stuff HTML into it and expat hands that
// markup back as literal text. The Book Info screen draws plain wrapped text, so
// flatten it once, at cache-build time: drop anything between angle brackets and
// collapse every run of whitespace to a single space.
std::string sanitizeDescription(const std::string& raw);

// The first JPG/PNG that the guide's cover page XHTML references (SVG xlink:href,
// then img src), resolved against the cover page's folder. Empty when none.
std::string findGuideCoverImage(std::string_view coverPageHtml, const std::string& coverPageHref);

}  // namespace OpfText
