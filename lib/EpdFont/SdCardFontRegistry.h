#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SdCardFontFileInfo {
  std::string path;   // v4 on-disk naming: "/<root>/<Family>/<Family>_<size>.cpfont"
                      // where <root> is "/.fonts" (preferred, hidden) or "/fonts" (visible).
                      // e.g. "/.fonts/NotoSansCJK/NotoSansCJK_14.cpfont"
  uint8_t pointSize;  // parsed from filename: 14
};

struct SdCardFontFamilyInfo {
  std::string name;  // directory name, e.g. "NotoSansCJK"
  std::vector<SdCardFontFileInfo> files;
#ifdef CROSSPOINT_TTF_READER
  // Vector faces found beside (or instead of) the .cpfont files, one full path
  // per EpdFontFamily style index (regular, bold, italic, bold-italic); empty
  // when the family has no such face. A family with any face here is a TTF
  // family: the reader loads it at the requested size and ignores its .cpfont
  // files (see SdCardFontManager::loadFamily). Resolution rules live in
  // TtfFamilyScan.h.
  std::string ttfFaces[4];
  bool hasTtf() const { return !ttfFaces[0].empty(); }
#else
  static constexpr bool hasTtf() { return false; }
#endif

  const SdCardFontFileInfo* findFile(uint8_t size) const;
  // Installed file closest to `pointSize` (ties → smaller). nullptr when the
  // family ships no .cpfont file.
  const SdCardFontFileInfo* findNearestSize(uint8_t pointSize) const;
  // Point sizes the reader can select for this family, ascending. A .cpfont
  // family ships fixed sizes; a TTF family offers every size in the TTF range.
  std::vector<uint8_t> availableSizes() const;
#ifdef CROSSPOINT_TTF_READER
  // The size the reader actually loads for a requested size: the nearest
  // shipped .cpfont size, or the request clamped into the TTF range. 0 when
  // the family has nothing loadable.
  uint8_t resolvePointSize(uint8_t pointSize) const;
#endif
};

class SdCardFontRegistry {
 public:
  static constexpr int MAX_SD_FAMILIES = 128;
  // Two top-level roots are scanned at discovery time. Hidden is preferred
  // when creating new installs; both are read from if present.
  static constexpr const char* FONTS_DIR_HIDDEN = "/.fonts";
  static constexpr const char* FONTS_DIR_VISIBLE = "/fonts";

  // Returns the existing root for `familyName` (the one that contains
  // /<root>/<familyName>/), or nullptr if the family is not installed in
  // either root. Used by writers to keep re-installs in their existing dir.
  static const char* findFamilyRoot(const char* familyName);

  // Returns the root path that should be used when creating a brand-new
  // family on disk (no prior install): the existing root if exactly one of
  // the two roots exists, otherwise the hidden root.
  static const char* defaultWriteRoot();

  // Scan SD card, populate families_. Returns true if any families found.
  bool discover();

  // Populate families_ with ONE named family, without walking either root's
  // directory listing. The wake path knows which family it wants from settings,
  // and a full discover() costs one directory probe per installed family to then
  // use exactly one of them. Returns false when the family is not installed, in
  // which case the caller must fall back to discover().
  bool discoverOne(const char* familyName);

  const std::vector<SdCardFontFamilyInfo>& getFamilies() const { return families_; }
  const SdCardFontFamilyInfo* findFamily(const std::string& name) const;
  int getFamilyIndex(const std::string& name) const;
  int getFamilyCount() const { return static_cast<int>(families_.size()); }

 private:
  std::vector<SdCardFontFamilyInfo> families_;  // sorted alphabetically

  static bool parseFilename(const char* filename, uint8_t& size);
  static void scanDirectory(const char* dirPath, SdCardFontFamilyInfo& family);
  // Scan one root (e.g. "/.fonts"), append families to `out`, dedup by name.
  static void scanRoot(const char* rootPath, std::vector<SdCardFontFamilyInfo>& out);
};
