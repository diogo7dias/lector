#pragma once
#include <Print.h>
#include <expat.h>

#include <string>

class BookMetadataCache;
class HalFile;

// Parser for EPUB 3 nav.xhtml navigation documents
// Parses HTML5 nav elements with epub:type="toc" to extract table of contents, and
// epub:type="page-list" (the printed edition's page numbers) into `pageList` as
// "label\thref\n" lines when a file is given.
class TocNavParser final : public Print {
  enum ParserState {
    START,
    IN_HTML,
    IN_BODY,
    IN_NAV_TOC,  // Inside <nav epub:type="toc">
    IN_OL,       // Inside <ol>
    IN_LI,       // Inside <li>
    IN_ANCHOR,   // Inside <a>
  };

  const std::string& baseContentPath;
  size_t remainingSize;
  XML_Parser parser = nullptr;
  ParserState state = START;
  BookMetadataCache* cache;
  HalFile* pageList;
  // True inside the page-list nav: its anchors are page numbers, not TOC entries.
  bool inPageList = false;
  uint16_t pageCount = 0;

  // Track nesting depth for <ol> elements to determine TOC depth
  uint8_t olDepth = 0;
  // Current entry data being collected
  std::string currentLabel;
  std::string currentHref;

  void writePage();

  static void startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void characterData(void* userData, const XML_Char* s, int len);
  static void endElement(void* userData, const XML_Char* name);

 public:
  explicit TocNavParser(const std::string& baseContentPath, const size_t xmlSize, BookMetadataCache* cache,
                        HalFile* pageList = nullptr)
      : baseContentPath(baseContentPath), remainingSize(xmlSize), cache(cache), pageList(pageList) {}
  ~TocNavParser() override;

  bool setup();
  // Page-list entries written to `pageList`.
  uint16_t pageListCount() const { return pageCount; }

  size_t write(uint8_t) override;
  size_t write(const uint8_t* buffer, size_t size) override;
};
