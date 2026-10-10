#include "BookSearch.h"

#include <Logging.h>
#include <Print.h>
#include <Utf8.h>
#include <XmlParserUtils.h>
#include <expat.h>
#include <strings.h>

#include <cstring>

#include "Epub.h"
#include "Epub/SearchMatcher.h"
#include "Epub/VisibleTextUtils.h"
#include "Epub/htmlEntities.h"
#include "Epub/parsers/VoidTagFixer.h"

namespace book_search {
namespace {

constexpr size_t CHUNK = 1024;
// Room for the carried tag in front of a chunk, and VoidTagFixer's one byte per void element.
constexpr size_t CAPACITY = CHUNK + VoidTagFixer::MAX_CARRY + CHUNK / 2;

// The visible-text half of ChapterHtmlSlimParser: the same body and non-visible tracking, the
// same entity expansion and void-tag repair, and offsets counted per codepoint of character
// data. Any rule added to the parser's counting must be mirrored here, or hits drift by page.
class ChapterTextWalker final : public Print {
 public:
  ChapterTextWalker(SearchMatcher& matcher, HitFn onHit, void* ctx) : matcher(matcher), onHit(onHit), ctx(ctx) {}
  ~ChapterTextWalker() override { destroyXmlParser(parser); }

  bool setup() {
    parser = XML_ParserCreate(nullptr);
    if (!parser) return false;
    XML_SetUserData(parser, this);
    XML_SetDefaultHandlerExpand(parser, defaultHandler);
    XML_SetElementHandler(parser, startElement, endElement);
    XML_SetCharacterDataHandler(parser, characterData);
    return true;
  }

  size_t write(const uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    const size_t total = size;
    while (size > 0 && parser) {
      const size_t take = size < CHUNK ? size : CHUNK;
      parseChunk(data, take, false);
      data += take;
      size -= take;
    }
    return total;  // a parse error ends the chapter quietly; the stream need not stop early
  }

  void finish() {
    if (parser) parseChunk(nullptr, 0, true);
    auto hit = [this](uint32_t off, const std::string& s) { onHit(ctx, off, s); };
    matcher.finish(hit);
  }

 private:
  void parseChunk(const uint8_t* data, const size_t size, const bool last) {
    auto* chars = static_cast<char*>(XML_GetBuffer(parser, CAPACITY));
    if (!chars) {
      LOG_ERR("SRCH", "Couldn't allocate parse buffer");
      destroyXmlParser(parser);
      return;
    }
    size_t len = carryLen;
    if (carryLen > 0) memcpy(chars, carry, carryLen);
    carryLen = 0;
    if (size > 0) memcpy(chars + len, data, size);
    len = VoidTagFixer::process(chars, len + size, CAPACITY, carry, carryLen, last);
    if (XML_ParseBuffer(parser, static_cast<int>(len), last) == XML_STATUS_ERROR) {
      if (!htmlEnded) {
        LOG_DBG("SRCH", "Parse error at line %lu: %s", XML_GetCurrentLineNumber(parser),
                XML_ErrorString(XML_GetErrorCode(parser)));
      }
      destroyXmlParser(parser);
    }
  }

  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char**) {
    auto* self = static_cast<ChapterTextWalker*>(userData);
    if (strcasecmp(name, "body") == 0) self->insideBody = true;
    if (self->insideBody && (self->nonVisibleDepth > 0 || VisibleTextUtils::isNonVisibleElement(name))) {
      self->nonVisibleDepth++;
    }
  }

  static void XMLCALL endElement(void* userData, const XML_Char* name) {
    auto* self = static_cast<ChapterTextWalker*>(userData);
    if (self->nonVisibleDepth > 0) self->nonVisibleDepth--;
    if (strcmp(name, "body") == 0) self->insideBody = false;
    if (strcmp(name, "html") == 0) self->htmlEnded = true;
  }

  static void XMLCALL characterData(void* userData, const XML_Char* s, const int len) {
    auto* self = static_cast<ChapterTextWalker*>(userData);
    if (!self->insideBody || self->nonVisibleDepth != 0) return;
    auto hit = [self](uint32_t off, const std::string& snippet) { self->onHit(self->ctx, off, snippet); };
    const auto* p = reinterpret_cast<const unsigned char*>(s);
    const auto* end = p + len;
    while (p < end) self->matcher.feed(utf8NextCodepoint(&p), self->offset++, hit);
  }

  // HTML entities expat does not know (&nbsp; and friends) arrive here, as in the parser.
  static void XMLCALL defaultHandler(void* userData, const XML_Char* s, const int len) {
    if (len < 3 || s[0] != '&' || s[len - 1] != ';') return;
    const char* value = lookupHtmlEntity(s, static_cast<size_t>(len));
    if (value) {
      characterData(userData, value, static_cast<int>(strlen(value)));
    } else {
      characterData(userData, s, len);
    }
  }

  SearchMatcher& matcher;
  HitFn onHit;
  void* ctx;
  XML_Parser parser = nullptr;
  char carry[VoidTagFixer::MAX_CARRY] = {};
  size_t carryLen = 0;
  bool insideBody = false;
  bool htmlEnded = false;
  int nonVisibleDepth = 0;
  uint32_t offset = 0;
};

}  // namespace

bool searchSpineItem(const Epub& epub, const int spineIndex, SearchMatcher& matcher, const HitFn onHit, void* ctx) {
  ChapterTextWalker walker(matcher, onHit, ctx);
  if (!walker.setup()) {
    LOG_ERR("SRCH", "Couldn't allocate XML parser");
    return false;
  }
  matcher.reset();
  if (!epub.readItemContentsToStream(epub.getSpineItem(spineIndex).href, walker, CHUNK)) {
    LOG_ERR("SRCH", "Could not read spine item %d", spineIndex);
    return false;
  }
  walker.finish();
  return true;
}

}  // namespace book_search
