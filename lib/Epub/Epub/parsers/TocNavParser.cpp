#include "TocNavParser.h"

#include <Logging.h>
#include <XmlParserUtils.h>

#include "Epub/BookMetadataCache.h"

bool TocNavParser::setup() {
  parser = XML_ParserCreate(nullptr);
  if (!parser) {
    LOG_DBG("NAV", "Couldn't allocate memory for parser");
    return false;
  }

  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);
  return true;
}

TocNavParser::~TocNavParser() { destroyXmlParser(parser); }

size_t TocNavParser::write(const uint8_t data) { return write(&data, 1); }

size_t TocNavParser::write(const uint8_t* buffer, const size_t size) {
  return feedXmlParser(parser, buffer, size, remainingSize, "NAV");
}

void XMLCALL TocNavParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<TocNavParser*>(userData);

  // Track HTML structure loosely - we mainly care about finding <nav epub:type="toc">
  if (xmlLocalNameEquals(name, "html")) {
    self->state = IN_HTML;
    return;
  }

  if (self->state == IN_HTML && xmlLocalNameEquals(name, "body")) {
    self->state = IN_BODY;
    return;
  }

  // Look for <nav epub:type="toc"> anywhere in body (or nested elements)
  if (self->state >= IN_BODY && xmlLocalNameEquals(name, "nav")) {
    for (int i = 0; atts[i]; i += 2) {
      if ((strcmp(atts[i], "epub:type") == 0 || strcmp(atts[i], "type") == 0) && strcmp(atts[i + 1], "toc") == 0) {
        self->state = IN_NAV_TOC;
        LOG_DBG("NAV", "Found nav toc element");
        return;
      }
    }
    return;
  }

  // Only process ol/li/a if we're inside the toc nav
  if (self->state < IN_NAV_TOC) {
    return;
  }

  if (xmlLocalNameEquals(name, "ol")) {
    self->olDepth++;
    self->state = IN_OL;
    return;
  }

  if (self->state == IN_OL && xmlLocalNameEquals(name, "li")) {
    self->state = IN_LI;
    self->currentLabel.clear();
    self->currentHref.clear();
    return;
  }

  if (self->state == IN_LI && xmlLocalNameEquals(name, "a")) {
    self->state = IN_ANCHOR;
    // Get href attribute
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "href") == 0) {
        self->currentHref = atts[i + 1];
        break;
      }
    }
    return;
  }
}

void XMLCALL TocNavParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<TocNavParser*>(userData);

  // Only collect text when inside an anchor within the TOC nav
  if (self->state == IN_ANCHOR) {
    self->currentLabel.append(s, len);
  }
}

void XMLCALL TocNavParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<TocNavParser*>(userData);

  if (xmlLocalNameEquals(name, "a") && self->state == IN_ANCHOR) {
    // Create TOC entry when closing anchor tag (we have all data now)
    if (!self->currentLabel.empty() && !self->currentHref.empty()) {
      if (self->cache) {
        // olDepth gives us the nesting level (1-based from the outer ol)
        self->cache->createTocEntry(self->currentLabel, self->baseContentPath + self->currentHref, self->olDepth);
      }

      self->currentLabel.clear();
      self->currentHref.clear();
    }
    self->state = IN_LI;
    return;
  }

  if (xmlLocalNameEquals(name, "li") && (self->state == IN_LI || self->state == IN_OL)) {
    self->state = IN_OL;
    return;
  }

  if (xmlLocalNameEquals(name, "ol") && self->state >= IN_NAV_TOC) {
    self->olDepth--;
    if (self->olDepth == 0) {
      self->state = IN_NAV_TOC;
    } else {
      self->state = IN_LI;  // Back to parent li
    }
    return;
  }

  if (xmlLocalNameEquals(name, "nav") && self->state >= IN_NAV_TOC) {
    self->state = IN_BODY;
    LOG_DBG("NAV", "Finished parsing nav toc");
    return;
  }
}
