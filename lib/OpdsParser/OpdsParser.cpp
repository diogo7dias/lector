#include "OpdsParser.h"

#include <Logging.h>
#include <XmlParserUtils.h>

#include <cctype>
#include <cstring>
#include <new>

#if defined(ESP32)
#include <Esp.h>
#endif

namespace {
constexpr size_t MAX_ENTRIES = OpdsParser::maxEntries();
// Text budget for every collected entry together, claimed once before the
// transfer. C3 keeps 48 entries + summaries in 14 KB so wolfSSL stays safe;
// X4 Pro (PSRAM) holds 64 entries with full 400 B summaries in 32 KB.
#if defined(BOARD_HAS_PSRAM)
constexpr size_t ARENA_BYTES = 32 * 1024;
#else
constexpr size_t ARENA_BYTES = 14 * 1024;
#endif
constexpr size_t MAX_TITLE_CHARS = 160;
constexpr size_t MAX_AUTHOR_CHARS = 120;
constexpr size_t MAX_HREF_CHARS = 768;
constexpr size_t MAX_SUMMARY_CHARS = OpdsParser::MAX_SUMMARY_CHARS;
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

bool hrefEndsWithIgnoreCase(const char* href, const char* ext) {
  if (!href || !ext) return false;
  size_t pathLen = 0;
  while (href[pathLen] && href[pathLen] != '?' && href[pathLen] != '#') ++pathLen;
  const size_t extLen = strlen(ext);
  if (pathLen < extLen) return false;
  const char* tail = href + pathLen - extLen;
  for (size_t i = 0; i < extLen; ++i) {
    if (std::tolower(static_cast<unsigned char>(tail[i])) != std::tolower(static_cast<unsigned char>(ext[i]))) {
      return false;
    }
  }
  return true;
}

// Classifies an OPDS acquisition link into a supported book format and preference
// rank (higher wins when an entry offers multiple formats: plain EPUB > derived
// EPUB > CBZ/XTC > TXT). Returns 0 if unsupported.
uint8_t classifyAcquisition(const char* type, const char* href, OpdsBookFormat& format) {
  const bool isEpubMime = type && strcmp(type, "application/epub+zip") == 0;
  const bool isPlainEpubHref = href && (hrefEndsWithIgnoreCase(href, ".epub") || strstr(href, "/epub/") != nullptr);
  if (isEpubMime || (!type && isPlainEpubHref)) {
    format = OpdsBookFormat::EPUB;
    return isPlainEpubHref ? 4 : 3;
  }
  if ((type && (strcmp(type, "application/vnd.comicbook+zip") == 0 || strcmp(type, "application/x-cbz") == 0)) ||
      hrefEndsWithIgnoreCase(href, ".cbz")) {
    format = OpdsBookFormat::CBZ;
    return 2;
  }
  if ((type && strstr(type, "xtc") != nullptr) || hrefEndsWithIgnoreCase(href, ".xtc") ||
      hrefEndsWithIgnoreCase(href, ".xtch")) {
    format = OpdsBookFormat::XTC;
    return 2;
  }
  if ((type && strncmp(type, "text/plain", 10) == 0) || hrefEndsWithIgnoreCase(href, ".txt")) {
    format = OpdsBookFormat::TXT;
    return 1;
  }
  return 0;
}

// Strips HTML tags and collapses whitespace in place, leaving a clean single
// paragraph bounded by MAX_SUMMARY_CHARS without allocating.
void cleanSummaryInPlace(std::string& s) {
  size_t out = 0;
  bool inTag = false;
  bool lastSpace = true;
  for (size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == '<') {
      inTag = true;
      continue;
    }
    if (inTag) {
      if (c == '>') inTag = false;
      continue;
    }
    if (c <= ' ') {
      if (!lastSpace && out < MAX_SUMMARY_CHARS) {
        s[out++] = ' ';
        lastSpace = true;
      }
    } else if (out < MAX_SUMMARY_CHARS) {
      s[out++] = static_cast<char>(c);
      lastSpace = false;
    }
  }
  while (out > 0 && s[out - 1] == ' ') --out;
  // Avoid cutting inside a multi-byte UTF-8 sequence.
  if (out > 0 && (static_cast<unsigned char>(s[out - 1]) & 0x80) != 0) {
    size_t back = out;
    while (back > 0 && (static_cast<unsigned char>(s[back - 1]) & 0xC0) == 0x80) --back;
    if (back > 0) {
      const unsigned char lead = static_cast<unsigned char>(s[back - 1]);
      size_t expected = 1;
      if ((lead & 0xE0) == 0xC0)
        expected = 2;
      else if ((lead & 0xF0) == 0xE0)
        expected = 3;
      else if ((lead & 0xF8) == 0xF0)
        expected = 4;
      if (out - (back - 1) < expected) out = back - 1;
    }
  }
  s.resize(out);
}
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
  currentEntry.summary.reserve(MAX_SUMMARY_CHARS);
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
  if (XML_Parse(parser, "", 0, XML_TRUE) != XML_STATUS_OK) {
    errorOccured = true;
    destroyXmlParser(parser);
  }
}

bool OpdsParser::error() const { return errorOccured; }

bool OpdsParser::storeCurrentEntry() {
  if (slots.size() >= MAX_ENTRIES || !arena) return false;
  const size_t baseNeed = currentEntry.title.size() + currentEntry.author.size() + currentEntry.href.size();
  if (arenaUsed + baseNeed > ARENA_BYTES) return false;

  // Trim summary only if the remaining arena cannot hold all of it, so a feed
  // full of long synopses does not drop book entries early.
  size_t summaryKeep = currentEntry.summary.size();
  const size_t availForSummary = ARENA_BYTES - arenaUsed - baseNeed;
  if (summaryKeep > availForSummary) {
    summaryKeep = availForSummary;
    while (summaryKeep > 0 && (static_cast<unsigned char>(currentEntry.summary[summaryKeep - 1]) & 0xC0) == 0x80) {
      --summaryKeep;
    }
    if (summaryKeep > 0 && (static_cast<unsigned char>(currentEntry.summary[summaryKeep - 1]) & 0x80) != 0) {
      --summaryKeep;
    }
  }

  Slot slot;
  slot.type = currentEntry.type;
  slot.format = currentEntry.format;
  const auto put = [this](const char* data, const size_t size, uint16_t& off, uint16_t& len) {
    off = static_cast<uint16_t>(arenaUsed);
    len = static_cast<uint16_t>(size);
    if (size > 0) memcpy(arena.get() + arenaUsed, data, size);
    arenaUsed += size;
  };
  put(currentEntry.title.data(), currentEntry.title.size(), slot.titleOff, slot.titleLen);
  put(currentEntry.author.data(), currentEntry.author.size(), slot.authorOff, slot.authorLen);
  put(currentEntry.href.data(), currentEntry.href.size(), slot.hrefOff, slot.hrefLen);
  put(currentEntry.summary.data(), summaryKeep, slot.summaryOff, slot.summaryLen);
  slots.push_back(slot);
  return true;
}

std::vector<OpdsEntry> OpdsParser::takeEntries() {
  std::vector<OpdsEntry> out;
  out.reserve(slots.size());
  for (const Slot& slot : slots) {
    OpdsEntry entry;
    entry.type = slot.type;
    entry.format = slot.format;
    entry.title.assign(arena.get() + slot.titleOff, slot.titleLen);
    entry.author.assign(arena.get() + slot.authorOff, slot.authorLen);
    entry.href.assign(arena.get() + slot.hrefOff, slot.hrefLen);
    entry.summary.assign(arena.get() + slot.summaryOff, slot.summaryLen);
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
    self->currentEntry.format = OpdsBookFormat::EPUB;
    self->currentEntry.title.clear();
    self->currentEntry.author.clear();
    self->currentEntry.href.clear();
    self->currentEntry.summary.clear();
    self->currentText.clear();
    self->currentLinkPriority = 0;
    self->inTitle = self->inAuthor = self->inAuthorName = self->inSummary = false;
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
        if (rel && strstr(rel, "opds-spec.org/acquisition") != nullptr) {
          OpdsBookFormat fmt = OpdsBookFormat::EPUB;
          const uint8_t priority = classifyAcquisition(type, href, fmt);
          if (priority > self->currentLinkPriority) {
            self->currentLinkPriority = priority;
            self->currentEntry.type = OpdsEntryType::BOOK;
            self->currentEntry.format = fmt;
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
  } else if (strcmp(name, "summary") == 0 || strstr(name, ":summary") != nullptr) {
    self->inSummary = true;
    self->currentEntry.summary.clear();
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
    self->inSummary = false;
  } else if (self->inEntry) {
    if (strcmp(name, "title") == 0 || strstr(name, ":title") != nullptr) {
      if (self->inTitle) self->currentEntry.title = self->currentText;
      self->inTitle = false;
    } else if (strcmp(name, "author") == 0 || strstr(name, ":author") != nullptr) {
      self->inAuthor = false;
    } else if (self->inAuthorName && (strcmp(name, "name") == 0 || strstr(name, ":name") != nullptr)) {
      self->currentEntry.author = self->currentText;
      self->inAuthorName = false;
    } else if (strcmp(name, "summary") == 0 || strstr(name, ":summary") != nullptr) {
      if (self->inSummary) cleanSummaryInPlace(self->currentEntry.summary);
      self->inSummary = false;
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
  } else if (self->inSummary) {
    appendBounded(self->currentEntry.summary, s, len, MAX_SUMMARY_CHARS);
  }
}
