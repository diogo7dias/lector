#pragma once
#include <Print.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "Epub.h"
#include "expat.h"

class BookMetadataCache;

class ContentOpfParser final : public Print {
  enum ParserState {
    START,
    IN_PACKAGE,
    IN_METADATA,
    IN_BOOK_TITLE,
    IN_BOOK_AUTHOR,
    IN_BOOK_LANGUAGE,
    IN_BOOK_DESCRIPTION,
    IN_MANIFEST,
    IN_SPINE,
    IN_GUIDE,
  };

  const std::string& cachePath;
  const std::string& baseContentPath;
  size_t remainingSize;
  XML_Parser parser = nullptr;
  ParserState state = START;
  BookMetadataCache* cache;
  HalFile tempItemStore;
  std::string coverItemId;

  // Index for fast idref→href lookup (used only for large EPUBs)
  struct ItemIndexEntry {
    uint32_t idHash;      // FNV-1a hash of itemId
    uint16_t idLen;       // length for collision reduction
    uint32_t fileOffset;  // offset in .items.bin
  };
  // Flat nothrow-growing array (was std::deque: its push_back growth uses the
  // THROWING allocator, which abort()s on OOM under -fno-exceptions). The index
  // is only an optimization; when it cannot grow it is dropped entirely and
  // spine lookup falls back to the linear scan.
  std::unique_ptr<ItemIndexEntry[]> itemIndex;
  uint32_t itemIndexCapacity = 0;
  uint32_t itemIndexCount = 0;
  bool itemIndexFailed = false;
  bool useItemIndex = false;

  static constexpr uint16_t LARGE_SPINE_THRESHOLD = 400;
  static constexpr uint32_t ITEM_INDEX_INITIAL_ENTRIES = 128;
  static constexpr uint32_t ITEM_INDEX_MAX_ENTRIES = 65535;

  // Appends to the index with nothrow growth. On OOM (or at the entry cap) the
  // whole index is released and itemIndexFailed set: a partial index would make
  // the binary search silently miss items.
  void pushItemIndexEntry(const ItemIndexEntry& entry);

  // FNV-1a hash function
  static uint32_t fnvHash(const std::string& s) {
    uint32_t hash = 2166136261u;
    for (char c : s) {
      hash ^= static_cast<uint8_t>(c);
      hash *= 16777619u;
    }
    return hash;
  }

  static void startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void characterData(void* userData, const XML_Char* s, int len);
  static void endElement(void* userData, const XML_Char* name);

 public:
  std::string title;
  std::string author;
  std::string language;
  std::string description;  // dc:description; may contain HTML that is stripped downstream
  std::string tocNcxPath;
  std::string tocNavPath;  // EPUB 3 nav document path
  std::string coverItemHref;
  std::string guideCoverPageHref;  // Guide reference with type="cover" or "cover-page" (points to XHTML wrapper)
  std::string textReferenceHref;
  std::vector<std::string> cssFiles;  // CSS stylesheet paths

  explicit ContentOpfParser(const std::string& cachePath, const std::string& baseContentPath, const size_t xmlSize,
                            BookMetadataCache* cache)
      : cachePath(cachePath), baseContentPath(baseContentPath), remainingSize(xmlSize), cache(cache) {}
  ~ContentOpfParser() override;

  bool setup();

  size_t write(uint8_t) override;
  size_t write(const uint8_t* buffer, size_t size) override;
};
