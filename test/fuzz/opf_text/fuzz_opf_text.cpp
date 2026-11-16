// libFuzzer harness for the OPF text clean-up in Epub::parseContentOpf: dc:title and
// dc:description through OpfText::sanitizeDescription and utf8ComposeNfc, and the guide
// cover-page scan. Input: the guide's cover page href, a NUL, then the cover page XHTML,
// which also stands in for the description and title text.
#include <Utf8.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "Epub/OpfText.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const char* text = reinterpret_cast<const char*>(data);
  const void* nul = memchr(text, 0, size);
  const size_t hrefSize = nul ? static_cast<const char*>(nul) - text : 0;
  const std::string href(text, hrefSize);
  const std::string page = nul ? std::string(text + hrefSize + 1, size - hrefSize - 1) : std::string(text, size);

  const std::string flat = OpfText::sanitizeDescription(page);
  // Plain wrapped text: no tags, no control bytes, single spaces only between words.
  if (flat.find_first_of("<") != std::string::npos || flat.find("  ") != std::string::npos ||
      (!flat.empty() && (flat.front() == ' ' || flat.back() == ' '))) {
    abort();
  }
  for (const char c : flat) {
    if (static_cast<unsigned char>(c) < ' ') abort();
  }
  const std::string description = utf8ComposeNfc(flat);
  const std::string title = utf8ComposeNfc(page);

  // The cover href is a zip entry name: no empty, "." or ".." segment survives normalisation.
  const std::string cover = OpfText::findGuideCoverImage(page, href);
  const std::string segments = "/" + cover + "/";
  if (!cover.empty() && (segments.find("//") != std::string::npos || segments.find("/./") != std::string::npos ||
                         segments.find("/../") != std::string::npos)) {
    abort();
  }
  if (getenv("FUZZ_TRACE"))
    fprintf(stderr, "description %zu bytes, title %zu bytes, cover \"%s\"\n", description.size(), title.size(),
            cover.c_str());
  return 0;
}
