#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "StreamingJsonParser.h"

// SAX parse of the font library's fonts.json, sized for the heap left over once WiFi
// is up. The document is ~37 KB and an in-RAM DOM of it costs upwards of 60 KB, which
// a fragmented heap cannot hand out: FontDownloadActivity used to abort() there with a
// bad_alloc thrown out of a vector growing while the DOM was still resident. Nothing is
// kept here except the fields the download screen reads, in fixed buffers, so the whole
// parsed manifest is ~13 KB and every allocation goes through the nothrow path.

struct FontManifestFile {
  static constexpr size_t MAX_NAME = 48;

  char name[MAX_NAME] = {};
  uint32_t size = 0;
  uint32_t crc32 = 0;
};

struct FontManifestFamily {
  static constexpr size_t MAX_NAME = 32;
  static constexpr size_t MAX_DESCRIPTION = 96;

  char name[MAX_NAME] = {};
  char description[MAX_DESCRIPTION] = {};
  std::vector<FontManifestFile> files;
  uint32_t totalSize = 0;

  // Disk state, not manifest state: filled in by the caller after the parse, and
  // updated again as families are installed or deleted.
  bool installed = false;
  bool hasUpdate = false;
};

class FontManifestParser {
 public:
  // Caps, not predictions. The published manifest carries 26 families of at most 9
  // files; these leave room to grow while keeping the worst case bounded on a device
  // that cannot afford an unbounded one.
  static constexpr size_t MAX_FAMILIES = 96;
  static constexpr size_t MAX_FILES_PER_FAMILY = 32;

  FontManifestParser();

  FontManifestParser(const FontManifestParser&) = delete;
  FontManifestParser& operator=(const FontManifestParser&) = delete;

  void reset();
  void feed(const char* data, size_t len);
  // Call once the last byte has been fed. A document that stops mid-way is not an
  // error the SAX parser can see on its own: it simply never hears the closing brace.
  void finish();

  // True when the document is malformed, a file entry lacks crc32, a cap was hit, or
  // an allocation failed. The three latter cases are also reported on their own below.
  bool hasError() const { return error_; }
  // A cap was exceeded: the manifest outgrew what this device is willing to hold.
  bool tooLarge() const { return tooLarge_; }
  // An allocation failed. The caller shows "not enough memory", never abort().
  bool outOfMemory() const { return outOfMemory_; }

  int version() const { return version_; }
  const char* baseUrl() const { return baseUrl_; }

  const std::vector<FontManifestFamily>& families() const { return families_; }
  std::vector<FontManifestFamily>& families() { return families_; }

 private:
  enum class Pos : uint8_t {
    TOP,
    FAMILIES,
    FAMILY,
    FILES,
    FILE,
  };

  enum class Key : uint8_t {
    NONE,
    VERSION,
    BASE_URL,
    FAMILIES,
    FAMILY_NAME,
    FAMILY_DESCRIPTION,
    FAMILY_FILES,
    FILE_NAME,
    FILE_SIZE,
    FILE_CRC32,
  };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  void beginFamily();
  void commitFamily();
  void beginFile();
  void commitFile();

  StreamingJsonParser parser_;

  Pos pos_ = Pos::TOP;
  Key key_ = Key::NONE;
  // Depth inside a container the manifest has no use for (scriptGroups, styles,
  // scripts, anything added later). Everything is swallowed until it closes.
  uint16_t skipDepth_ = 0;
  bool rootOpen_ = false;
  bool rootClosed_ = false;

  int version_ = 0;
  char baseUrl_[192] = {};

  std::vector<FontManifestFamily> families_;
  FontManifestFamily currentFamily_;
  FontManifestFile currentFile_;
  bool currentFileHasCrc32_ = false;

  bool error_ = false;
  bool tooLarge_ = false;
  bool outOfMemory_ = false;
};
