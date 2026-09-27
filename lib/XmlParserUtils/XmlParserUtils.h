#pragma once

#include <Logging.h>
#include <expat.h>

#include <cstdint>
#include <cstring>

// Safely tear down an expat parser: stop processing, clear callbacks, free, and null the pointer.
inline void destroyXmlParser(XML_Parser& parser) {
  if (!parser) return;
  XML_StopParser(parser, XML_FALSE);
  XML_SetElementHandler(parser, nullptr, nullptr);
  XML_SetCharacterDataHandler(parser, nullptr);
  XML_ParserFree(parser);
  parser = nullptr;
}

inline const char* xmlLocalName(const char* qName) {
  if (!qName) return "";
  const char* const separator = std::strchr(qName, ':');
  return separator ? separator + 1 : qName;
}

inline bool xmlLocalNameEquals(const char* qName, const char* expected) {
  return std::strcmp(xmlLocalName(qName), expected) == 0;
}

// Print::write() body shared by the streaming EPUB parsers: feeds `size` bytes to
// `parser` in 1KB chunks. `remainingSize` counts down the whole document so the
// chunk that ends it is flagged final. On failure the parser is destroyed and 0
// returned; later writes then see a null parser.
inline size_t feedXmlParser(XML_Parser& parser, const uint8_t* buffer, const size_t size, size_t& remainingSize,
                            const char* tag) {
  (void)tag;  // unused when logging is compiled out
  if (!parser) return 0;

  const uint8_t* currentBufferPos = buffer;
  auto remainingInBuffer = size;

  while (remainingInBuffer > 0) {
    void* const buf = XML_GetBuffer(parser, 1024);
    if (!buf) {
      LOG_ERR(tag, "Couldn't allocate memory for buffer");
      destroyXmlParser(parser);
      return 0;
    }

    const auto toRead = remainingInBuffer < 1024 ? remainingInBuffer : 1024;
    memcpy(buf, currentBufferPos, toRead);

    if (XML_ParseBuffer(parser, static_cast<int>(toRead), remainingSize == toRead) == XML_STATUS_ERROR) {
      LOG_DBG(tag, "Parse error at line %lu: %s", XML_GetCurrentLineNumber(parser),
              XML_ErrorString(XML_GetErrorCode(parser)));
      destroyXmlParser(parser);
      return 0;
    }

    currentBufferPos += toRead;
    remainingInBuffer -= toRead;
    remainingSize -= toRead;
  }
  return size;
}
