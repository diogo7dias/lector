#include "OpfText.h"

#include <FsHelpers.h>

#include <cstring>

namespace OpfText {

std::string sanitizeDescription(const std::string& raw) {
  std::string out;
  out.reserve(raw.size());

  bool inTag = false;
  bool pendingSpace = false;
  for (const char c : raw) {
    if (c == '<') {
      inTag = true;
      pendingSpace = true;  // a tag boundary is a word boundary
      continue;
    }
    if (inTag) {
      if (c == '>') inTag = false;
      continue;
    }
    if (static_cast<unsigned char>(c) <= ' ') {
      pendingSpace = true;
      continue;
    }
    if (pendingSpace && !out.empty()) {
      out.push_back(' ');
    }
    pendingSpace = false;
    out.push_back(c);
  }
  return out;
}

std::string findGuideCoverImage(const std::string_view coverPageHtml, const std::string& coverPageHref) {
  // Determine base path of the cover page for resolving relative image references
  std::string coverPageBase;
  const auto lastSlash = coverPageHref.rfind('/');
  if (lastSlash != std::string::npos) {
    coverPageBase = coverPageHref.substr(0, lastSlash + 1);
  }

  // Search for image references: xlink:href="..." (SVG) and src="..." (img)
  std::string imageRef;
  for (const char* pattern : {"xlink:href=\"", "src=\""}) {
    auto pos = coverPageHtml.find(pattern);
    while (pos != std::string_view::npos) {
      pos += strlen(pattern);
      const auto endPos = coverPageHtml.find('"', pos);
      if (endPos != std::string_view::npos) {
        const auto ref = coverPageHtml.substr(pos, endPos - pos);
        // Cover BMP generation supports JPG/PNG only; skip GIF so an unsupported wrapper image
        // does not block a later supported cover reference.
        if (FsHelpers::hasPngExtension(ref) || FsHelpers::hasJpgExtension(ref)) {
          imageRef = ref;
          break;
        }
      }
      pos = coverPageHtml.find(pattern, pos);
    }
    if (!imageRef.empty()) break;
  }

  if (imageRef.empty()) return {};
  return FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(coverPageBase + imageRef));
}

}  // namespace OpfText
