#include "OpdsParser.h"

#include <Logging.h>
#include <XmlParserUtils.h>

#include <cstring>
#include <new>

#if defined(ESP32)
#include <Esp.h>
#endif

namespace {
constexpr size_t MAX_ENTRIES = 64;
// Text budget for every collected entry together, claimed once before the
// transfer. 64 entries of a Calibre-Web page cost about 7 KB of title, author
// and href; the rest is headroom for long hrefs.
constexpr size_t ARENA_BYTES = 10 * 1024;
constexpr size_t MAX_TITLE_CHARS = 160;
constexpr size_t MAX_AUTHOR_CHARS = 120;
constexpr size_t MAX_HREF_CHARS = 768;
constexpr size_t MAX_SEARCH_TEMPLATE_CHARS = 768;
constexpr size_t MAX_PAGE_URL_CHARS = 768;
// Reserved up front for the feed-level links, so assigning them mid-transfer
// does not allocate. Longer values still work, at the cost of one realloc.
constexpr size_t FEED_URL_RESERVE = 256;
#if defined(ESP32)
// Free heap below which the fetch is stopped on purpose. wolfSSL fails around
// 20 KB with an unreadable MEMORY_E (-125), so stop above that and report it.
constexpr size_t MIN_FREE_HEAP_BYTES = 15 * 1024;
#endif
}  // namespace

OpdsParser::OpdsParser() {
  parser = XML_ParserCreate(nullptr);
  if (!parser) {
    errorOccured = true;
    LOG_DBG("OPDS", "Couldn't allocate memory for parser");
    return;
  }
  // Everything the parse needs is claimed here, before the caller opens the TLS
  // session: during the transfer this object allocates nothing.
  arena.reset(new (std::nothrow) char[ARENA_BYTES]);
  if (!arena) {
    errorOccured = true;
    LOG_ERR("OPDS", "OOM: %u byte entry arena", (unsigned)ARENA_BYTES);
    return;
  }
  slots.reserve(MAX_ENTRIES);
  currentEntry.title.reserve(MAX_TITLE_CHARS);
  currentEntry.author.reserve(MAX_AUTHOR_CHARS);
  currentEntry.href.reserve(MAX_HREF_CHARS);
  currentText.reserve(MAX_TITLE_CHARS);
  searchTemplate.reserve(FEED_URL_RESERVE);
  nextPageUrl.reserve(FEED_URL_RESERVE);
  prevPageUrl.reserve(FEED_URL_RESERVE);
  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);
}

OpdsParser::~OpdsParser() { destroyXmlParser(parser); }

size_t OpdsParser::write(uint8_t c) { return write(&c, 1); }

size_t OpdsParser::write(const uint8_t* xmlData, const size_t length) {
  if (errorOccured) return length;

#if defined(ESP32)
  if (ESP.getFreeHeap() < MIN_FREE_HEAP_BYTES) {
    heapAbort = true;
    errorOccured = true;
    LOG_ERR("OPDS", "Aborting feed: free heap %u bytes, %u entries collected", (unsigned)ESP.getFreeHeap(),
            (unsigned)slots.size());
    destroyXmlParser(parser);
    // Short write: the downloader reads it as "stop", so the transfer ends here
    // instead of running the heap down to wolfSSL's MEMORY_E.
    return 0;
  }
#endif

  const char* currentPos = reinterpret_cast<const char*>(xmlData);
  size_t remaining = length;
  constexpr size_t chunkSize = 1024;

  while (remaining > 0) {
    const size_t toRead = remaining < chunkSize ? remaining : chunkSize;
    void* const buf = XML_GetBuffer(parser, toRead);
    if (!buf) {
      errorOccured = true;
      LOG_DBG("OPDS", "Couldn't allocate memory for buffer");
      destroyXmlParser(parser);
      return length;
    }

    memcpy(buf, currentPos, toRead);

    if (XML_ParseBuffer(parser, static_cast<int>(toRead), 0) == XML_STATUS_ERROR) {
      errorOccured = true;
      LOG_DBG("OPDS", "Parse error at line %lu: %s", XML_GetCurrentLineNumber(parser),
              XML_ErrorString(XML_GetErrorCode(parser)));
      destroyXmlParser(parser);
      return length;
    }
    currentPos += toRead;
    remaining -= toRead;
  }
  return length;
}

void OpdsParser::flush() {
  if (errorOccured || !parser) return;
  if (XML_Parse(parser, nullptr, 0, XML_TRUE) != XML_STATUS_OK) {
    errorOccured = true;
    destroyXmlParser(parser);
  }
}

bool OpdsParser::error() const { return errorOccured; }

void OpdsParser::clear() {
  slots.clear();
  arenaUsed = 0;
  searchTemplate.clear();
  nextPageUrl.clear();
  prevPageUrl.clear();
  currentEntry.type = OpdsEntryType::NAVIGATION;
  currentEntry.title.clear();
  currentEntry.author.clear();
  currentEntry.href.clear();
  currentText.clear();
  inEntry = inTitle = inAuthor = inAuthorName = false;
  collectCurrentEntry = false;
  feedTruncated = false;
}

bool OpdsParser::storeCurrentEntry() {
  if (slots.size() >= MAX_ENTRIES || !arena) return false;
  const size_t need = currentEntry.title.size() + currentEntry.author.size() + currentEntry.href.size();
  if (arenaUsed + need > ARENA_BYTES) return false;

  Slot slot;
  slot.type = currentEntry.type;
  const auto put = [this](const std::string& text, uint16_t& off, uint16_t& len) {
    off = static_cast<uint16_t>(arenaUsed);
    len = static_cast<uint16_t>(text.size());
    memcpy(arena.get() + arenaUsed, text.data(), text.size());
    arenaUsed += text.size();
  };
  put(currentEntry.title, slot.titleOff, slot.titleLen);
  put(currentEntry.author, slot.authorOff, slot.authorLen);
  put(currentEntry.href, slot.hrefOff, slot.hrefLen);
  slots.push_back(slot);
  return true;
}

std::vector<OpdsEntry> OpdsParser::takeEntries() {
  std::vector<OpdsEntry> out;
  out.reserve(slots.size());
  for (const Slot& slot : slots) {
    OpdsEntry entry;
    entry.type = slot.type;
    entry.title.assign(arena.get() + slot.titleOff, slot.titleLen);
    entry.author.assign(arena.get() + slot.authorOff, slot.authorLen);
    entry.href.assign(arena.get() + slot.hrefOff, slot.hrefLen);
    out.push_back(std::move(entry));
  }
  slots.clear();
  arenaUsed = 0;
  return out;
}

const char* OpdsParser::findAttribute(const XML_Char** atts, const char* name) {
  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(atts[i], name) == 0) return atts[i + 1];
  }
  return nullptr;
}

void OpdsParser::assignBounded(std::string& target, const char* value, const size_t maxLen) {
  if (!value) {
    target.clear();
    return;
  }
  target.assign(value, strnlen(value, maxLen));
}

void OpdsParser::appendBounded(std::string& target, const char* value, const size_t len, const size_t maxLen) {
  if (target.size() >= maxLen) return;
  const size_t remaining = maxLen - target.size();
  target.append(value, len < remaining ? len : remaining);
}

void XMLCALL OpdsParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<OpdsParser*>(userData);

  if (strcmp(name, "entry") == 0 || strstr(name, ":entry") != nullptr) {
    self->inEntry = true;
    self->collectCurrentEntry = self->slots.size() < MAX_ENTRIES && self->arenaUsed < ARENA_BYTES;
    self->feedTruncated = self->feedTruncated || !self->collectCurrentEntry;
    // Cleared field by field rather than reassigned: the strings keep the
    // capacity reserved in the constructor, so no entry allocates mid-transfer.
    self->currentEntry.type = OpdsEntryType::NAVIGATION;
    self->currentEntry.title.clear();
    self->currentEntry.author.clear();
    self->currentEntry.href.clear();
    self->currentText.clear();
    self->inTitle = self->inAuthor = self->inAuthorName = false;
    return;
  }

  if (strcmp(name, "link") == 0 || strstr(name, ":link") != nullptr) {
    const char* href = findAttribute(atts, "href");
    if (href) {
      const char* rel = findAttribute(atts, "rel");
      const char* type = findAttribute(atts, "type");

      if (rel && strcmp(rel, "search") == 0) {
        if (strstr(href, "{searchTerms}") != nullptr) {
          assignBounded(self->searchTemplate, href, MAX_SEARCH_TEMPLATE_CHARS);
        }
      } else if (rel && strcmp(rel, "next") == 0 && !self->inEntry) {
        assignBounded(self->nextPageUrl, href, MAX_PAGE_URL_CHARS);
      } else if (rel && strcmp(rel, "previous") == 0 && !self->inEntry) {
        assignBounded(self->prevPageUrl, href, MAX_PAGE_URL_CHARS);
      }

      if (self->inEntry && self->collectCurrentEntry) {
        if (rel && type && strstr(rel, "opds-spec.org/acquisition") != nullptr &&
            strcmp(type, "application/epub+zip") == 0) {
          // Prefer plain EPUB links over derived formats when multiple
          // acquisition links are present for one entry.
          const bool isPlainEpub = strstr(href, ".epub") != nullptr || strstr(href, "/epub/") != nullptr;
          const bool alreadyHasPlainEpub = self->currentEntry.type == OpdsEntryType::BOOK &&
                                           (self->currentEntry.href.find(".epub") != std::string::npos ||
                                            self->currentEntry.href.find("/epub/") != std::string::npos);
          if (self->currentEntry.type != OpdsEntryType::BOOK || (isPlainEpub && !alreadyHasPlainEpub)) {
            self->currentEntry.type = OpdsEntryType::BOOK;
            assignBounded(self->currentEntry.href, href, MAX_HREF_CHARS);
          }
        } else if (type && strstr(type, "application/atom+xml") != nullptr) {
          if (self->currentEntry.type != OpdsEntryType::BOOK) {
            self->currentEntry.type = OpdsEntryType::NAVIGATION;
            assignBounded(self->currentEntry.href, href, MAX_HREF_CHARS);
          }
        }
      }
    }
  }

  if (!self->inEntry || !self->collectCurrentEntry) return;

  if (strcmp(name, "title") == 0 || strstr(name, ":title") != nullptr) {
    self->inTitle = true;
    self->currentText.clear();
  } else if (strcmp(name, "author") == 0 || strstr(name, ":author") != nullptr) {
    self->inAuthor = true;
  } else if (self->inAuthor && (strcmp(name, "name") == 0 || strstr(name, ":name") != nullptr)) {
    self->inAuthorName = true;
    self->currentText.clear();
  }
}

void XMLCALL OpdsParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<OpdsParser*>(userData);

  if (strcmp(name, "entry") == 0 || strstr(name, ":entry") != nullptr) {
    if (self->collectCurrentEntry && !self->currentEntry.title.empty() && !self->currentEntry.href.empty()) {
      if (!self->storeCurrentEntry()) self->feedTruncated = true;
    }
    self->inEntry = false;
    self->collectCurrentEntry = false;
  } else if (self->inEntry) {
    if (strcmp(name, "title") == 0 || strstr(name, ":title") != nullptr) {
      if (self->inTitle) self->currentEntry.title = self->currentText;
      self->inTitle = false;
    } else if (strcmp(name, "author") == 0 || strstr(name, ":author") != nullptr) {
      self->inAuthor = false;
    } else if (self->inAuthorName && (strcmp(name, "name") == 0 || strstr(name, ":name") != nullptr)) {
      self->currentEntry.author = self->currentText;
      self->inAuthorName = false;
    }
  }
}

void XMLCALL OpdsParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<OpdsParser*>(userData);
  if (!self->collectCurrentEntry) return;
  if (self->inTitle) {
    appendBounded(self->currentText, s, len, MAX_TITLE_CHARS);
  } else if (self->inAuthorName) {
    appendBounded(self->currentText, s, len, MAX_AUTHOR_CHARS);
  }
}
