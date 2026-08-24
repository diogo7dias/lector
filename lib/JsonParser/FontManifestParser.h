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
  // Empty unless this family is the one being retained: see FileRetention. The
  // count survives either way, because the download screen needs "3 of 9" long
  // after the names themselves have been dropped.
  //
  // Nothing is allocated here while a family is being parsed. The parser fills a
  // fixed scratch array and copies into this vector only for a retained family:
  // growing a vector per family cost 896 bytes each, and on the device that
  // memory did not come back when the vector was cleared — 26 families burned
  // 23 KB of the heap a TLS session then had to fit into.
  std::vector<FontManifestFile> files;
  uint16_t fileCount = 0;
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
  // Held as a fixed array inside the parser, so this bound is also its cost:
  // 16 * sizeof(FontManifestFile) = 896 bytes, against 9 files per family today.
  static constexpr size_t MAX_FILES_PER_FAMILY = 16;

  // Which families keep their file lists once parsed. 26 families of 9 files cost
  // 27456 bytes resident, and on a reader that has just brought WiFi up, that is
  // the difference between a TLS handshake that completes and one that dies with
  // 4844 bytes free. Only the family actually being downloaded needs its names.
  enum class FileRetention : uint8_t {
    All,   // every family keeps its files (the default, and what tests use)
    None,  // no family keeps files; counts and sizes still add up
    One,   // only the family named by retainFilesFor(), and it is the only one kept at all
  };

  // Called as each family finishes parsing, while its file list is still intact
  // even when it is about to be dropped. This is where a caller stamps whatever
  // it can only work out from the names, such as what is already on the card.
  // The files are the parser's scratch array, valid only for this call.
  using FamilyHook = void (*)(void* context, FontManifestFamily& family, const FontManifestFile* files, size_t count);

  FontManifestParser();

  FontManifestParser(const FontManifestParser&) = delete;
  FontManifestParser& operator=(const FontManifestParser&) = delete;

  void retainFiles(FileRetention retention) { retention_ = retention; }
  void retainFilesFor(const char* familyName);
  void setFamilyHook(FamilyHook hook, void* context) {
    familyHook_ = hook;
    familyHookContext_ = context;
  }

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
  // The files of the family being parsed. Fixed storage, reused family after
  // family, so parsing a manifest allocates nothing per family.
  FontManifestFile fileScratch_[MAX_FILES_PER_FAMILY];
  uint16_t fileScratchCount_ = 0;
  bool currentFileHasCrc32_ = false;

  FileRetention retention_ = FileRetention::All;
  char retainedFamily_[FontManifestFamily::MAX_NAME] = {};
  FamilyHook familyHook_ = nullptr;
  void* familyHookContext_ = nullptr;

  bool error_ = false;
  bool tooLarge_ = false;
  bool outOfMemory_ = false;
};
