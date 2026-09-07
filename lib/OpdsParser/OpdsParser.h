#pragma once
#include <Print.h>
#include <expat.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/**
 * Type of OPDS entry.
 */
enum class OpdsEntryType {
  NAVIGATION,  // Link to another catalog
  BOOK         // Downloadable book
};

/**
 * Represents an entry from an OPDS feed (either a navigation link or a book).
 */
struct OpdsEntry {
  OpdsEntryType type = OpdsEntryType::NAVIGATION;
  std::string title;
  std::string author;  // Only for books
  std::string href;    // Navigation URL or epub download URL
};

/**
 * Parser for OPDS (Open Publication Distribution System) Atom feeds.
 * Uses the Expat XML parser to parse OPDS catalog entries.
 *
 * The feed is parsed as it arrives (see OpdsParserStream), so the body is never
 * held whole. What used to grow instead was the result: one std::string per
 * field per entry, allocated while the TLS session was live. On a large feed
 * (/opds/discover answers with 130 KB) that took ~22 KB of small blocks out of a
 * heap the framebuffer loan had already left at ~45 KB, and wolfSSL died
 * mid-body with MEMORY_E (-125).
 *
 * So the collected text goes into one fixed arena claimed in the constructor,
 * before the transfer starts: the heap does not move for the whole fetch, and a
 * feed larger than the arena holds is truncated (truncated() reports it) rather
 * than being allowed to eat the heap. Entries are materialised as std::string
 * by takeEntries(), after the transfer, when the heap is free again.
 *
 * Usage:
 *   OpdsParser parser;
 *   // ... feed bytes through write() ...
 *   for (const auto& entry : parser.takeEntries()) { ... }
 */
class OpdsParser final : public Print {
 public:
  OpdsParser();
  ~OpdsParser();

  // Disable copy
  const std::string& getSearchTemplate() const { return searchTemplate; }
  const std::string& getNextPageUrl() const { return nextPageUrl; }
  const std::string& getPrevPageUrl() const { return prevPageUrl; }
  OpdsParser(const OpdsParser&) = delete;
  OpdsParser& operator=(const OpdsParser&) = delete;

  size_t write(uint8_t) override;
  size_t write(const uint8_t*, size_t) override;

  void flush() override;

  bool error() const;
  bool truncated() const { return feedTruncated; }
  /** True when the fetch was stopped because free heap fell to the abort floor. */
  bool heapAborted() const { return heapAbort; }

  operator bool() { return !error(); }

  /**
   * Build the parsed entries (both navigation and book entries) and hand over
   * the arena's contents. Call once, after the transfer has ended.
   */
  std::vector<OpdsEntry> takeEntries();

  /** Number of entries collected so far. */
  size_t entryCount() const { return slots.size(); }

  /**
   * Clear all parsed entries.
   */
  void clear();

 private:
  // Expat callbacks
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL endElement(void* userData, const XML_Char* name);
  static void XMLCALL characterData(void* userData, const XML_Char* s, int len);

  // One collected entry, as offsets into the arena. 16 bytes, against ~100 for
  // the four std::strings it replaces during the transfer.
  struct Slot {
    uint16_t titleOff = 0;
    uint16_t titleLen = 0;
    uint16_t authorOff = 0;
    uint16_t authorLen = 0;
    uint16_t hrefOff = 0;
    uint16_t hrefLen = 0;
    OpdsEntryType type = OpdsEntryType::NAVIGATION;
  };

  // Copies the staged entry into the arena. False when it no longer fits.
  bool storeCurrentEntry();

  std::string searchTemplate;
  std::string nextPageUrl;
  std::string prevPageUrl;
  // Helper to find attribute value
  static const char* findAttribute(const XML_Char** atts, const char* name);
  static void assignBounded(std::string& target, const char* value, size_t maxLen);
  static void appendBounded(std::string& target, const char* value, size_t len, size_t maxLen);

  XML_Parser parser = nullptr;
  std::unique_ptr<char[]> arena;
  size_t arenaUsed = 0;
  std::vector<Slot> slots;
  OpdsEntry currentEntry;
  std::string currentText;

  // Parser state
  bool inEntry = false;
  bool inTitle = false;
  bool inAuthor = false;
  bool inAuthorName = false;
  bool collectCurrentEntry = false;

  bool errorOccured = false;
  bool feedTruncated = false;
  bool heapAbort = false;
};
