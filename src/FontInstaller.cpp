#include "FontInstaller.h"

#include <HalStorage.h>
#include <Logging.h>
#include <esp_task_wdt.h>

#include <cctype>
#include <cstring>

#include "CrossPointSettings.h"
#include "FontUploadPolicy.h"

namespace {
static_assert(font_upload::MAX_FAMILIES == SdCardFontRegistry::MAX_SD_FAMILIES,
              "Web upload and font registry family limits must match");
uint32_t readU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8); }
}  // namespace

FontInstaller::FontInstaller(SdCardFontRegistry& registry) : registry_(registry) {}

bool FontInstaller::isValidFamilyName(const char* name) {
  if (name == nullptr || name[0] == '\0') return false;
  if (!font_upload::lengthsAreSafe(strlen(name), 1)) return false;

  // Reject path traversal
  if (strstr(name, "..") != nullptr) return false;
  if (strchr(name, '/') != nullptr) return false;
  if (strchr(name, '\\') != nullptr) return false;

  for (const char* p = name; *p != '\0'; ++p) {
    char c = *p;
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
      return false;
    }
  }
  return true;
}

bool FontInstaller::isValidCpfontFilename(const char* name) {
  if (name == nullptr || name[0] == '\0') return false;

  // Reject path separators / traversal up front. Anything that could escape
  // the family directory or refer to a different one is a hard reject.
  if (strstr(name, "..") != nullptr) return false;
  if (strchr(name, '/') != nullptr) return false;
  if (strchr(name, '\\') != nullptr) return false;

  // Must end with ".cpfont" exactly.
  static constexpr char kExt[] = ".cpfont";
  static constexpr size_t kExtLen = sizeof(kExt) - 1;
  size_t nameLen = strlen(name);
  if (!font_upload::lengthsAreSafe(1, nameLen)) return false;
  if (nameLen <= kExtLen) return false;
  if (strcmp(name + nameLen - kExtLen, kExt) != 0) return false;

  // Basename (before .cpfont) must be alphanumeric + hyphen + underscore only.
  // No additional dots — keeps stray "Foo.cpfont.tmp"-style names out.
  size_t baseLen = nameLen - kExtLen;
  for (size_t i = 0; i < baseLen; ++i) {
    char c = name[i];
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
      return false;
    }
  }
  return true;
}

bool FontInstaller::ensureFamilyDir(const char* familyName) {
  // Reuse the family's existing root if installed; otherwise pick the
  // default-write root (hidden if no roots exist yet).
  const char* root = SdCardFontRegistry::findFamilyRoot(familyName);
  if (!root) root = SdCardFontRegistry::defaultWriteRoot();

  if (!Storage.exists(root)) {
    if (!Storage.mkdir(root)) {
      LOG_ERR("FONT", "Failed to create fonts dir: %s", root);
      return false;
    }
  }

  char dirPath[160];
  snprintf(dirPath, sizeof(dirPath), "%s/%s", root, familyName);

  if (!Storage.exists(dirPath)) {
    if (!Storage.mkdir(dirPath)) {
      LOG_ERR("FONT", "Failed to create family dir: %s", dirPath);
      return false;
    }
  }
  return true;
}

bool FontInstaller::validateCpfontFile(const char* path) {
  HalFile file;
  if (!Storage.openFileForRead("FONT", path, file)) {
    LOG_ERR("FONT", "Cannot open for validation: %s", path);
    return false;
  }

  FontUploadPolicy policy;
  uint8_t buffer[512];
  const size_t fileSize = file.size();
  size_t bytesSinceWatchdog = 0;
  while (file.available()) {
    const int bytesRead = file.read(buffer, sizeof(buffer));
    if (bytesRead <= 0 || !policy.add(buffer, static_cast<size_t>(bytesRead))) {
      file.close();
      LOG_ERR("FONT", "Failed while validating: %s", path);
      return false;
    }
    bytesSinceWatchdog += static_cast<size_t>(bytesRead);
    if (bytesSinceWatchdog >= 64 * 1024) {
      esp_task_wdt_reset();
      bytesSinceWatchdog = 0;
    }
  }
  if (!policy.finish(fileSize)) {
    file.close();
    LOG_ERR("FONT", "Invalid cpfont header or length: %s", path);
    return false;
  }

  uint8_t header[32];
  if (!file.seekSet(0) || file.read(header, sizeof(header)) != sizeof(header)) {
    file.close();
    return false;
  }
  const uint8_t styleCount = header[12];
  const bool is2Bit = (readU16(header + 10) & 1u) != 0;
  struct StyleLayout {
    uint32_t intervalCount;
    uint32_t glyphCount;
    uint32_t dataOffset;
    uint32_t glyphOffset;
    uint32_t bitmapOffset;
  } layouts[4]{};
  for (uint8_t style = 0; style < styleCount; ++style) {
    uint8_t toc[32];
    if (!file.seekSet(32 + static_cast<uint32_t>(style) * 32) || file.read(toc, sizeof(toc)) != sizeof(toc)) {
      file.close();
      return false;
    }
    auto& layout = layouts[style];
    layout.intervalCount = readU32(toc + 4);
    layout.glyphCount = readU32(toc + 8);
    layout.dataOffset = readU32(toc + 24);
    const uint64_t glyphOffset = static_cast<uint64_t>(layout.dataOffset) + layout.intervalCount * 12ull;
    const uint64_t bitmapOffset = glyphOffset + layout.glyphCount * 16ull + readU16(toc + 17) * 3ull +
                                  readU16(toc + 19) * 3ull + toc[21] * toc[22] + toc[23] * 8ull;
    if (glyphOffset > UINT32_MAX || bitmapOffset > fileSize) {
      file.close();
      return false;
    }
    layout.glyphOffset = static_cast<uint32_t>(glyphOffset);
    layout.bitmapOffset = static_cast<uint32_t>(bitmapOffset);
  }

  for (uint8_t style = 0; style < styleCount; ++style) {
    const auto& layout = layouts[style];
    uint32_t sectionEnd = static_cast<uint32_t>(fileSize);
    for (uint8_t other = 0; other < styleCount; ++other) {
      if (layouts[other].dataOffset > layout.dataOffset && layouts[other].dataOffset < sectionEnd) {
        sectionEnd = layouts[other].dataOffset;
      }
      if (other != style && layouts[other].dataOffset == layout.dataOffset) {
        file.close();
        return false;
      }
    }
    if (layout.bitmapOffset > sectionEnd || !file.seekSet(layout.glyphOffset)) {
      file.close();
      return false;
    }
    uint32_t expectedBitmapOffset = 0;
    for (uint32_t index = 0; index < layout.glyphCount; ++index) {
      if ((index & 0x3Fu) == 0) esp_task_wdt_reset();
      uint8_t glyph[16];
      if (file.read(glyph, sizeof(glyph)) != sizeof(glyph)) {
        file.close();
        return false;
      }
      const uint32_t dataLength = readU16(glyph + 8);
      const uint32_t bitmapBytesAvailable = sectionEnd - layout.bitmapOffset;
      if (!font_upload::glyphRecordIsSafe(glyph, is2Bit, expectedBitmapOffset, bitmapBytesAvailable)) {
        file.close();
        return false;
      }
      expectedBitmapOffset += dataLength;
    }

    if (!file.seekSet(layout.dataOffset)) {
      file.close();
      return false;
    }
    uint32_t expectedOffset = 0;
    uint32_t previousLast = 0;
    for (uint32_t index = 0; index < layout.intervalCount; ++index) {
      if ((index & 0x3Fu) == 0) esp_task_wdt_reset();
      uint8_t interval[12];
      if (file.read(interval, sizeof(interval)) != sizeof(interval)) {
        file.close();
        return false;
      }
      const uint32_t first = readU32(interval);
      const uint32_t last = readU32(interval + 4);
      const uint32_t offset = readU32(interval + 8);
      if (first > last || (index > 0 && first <= previousLast)) {
        file.close();
        return false;
      }
      const uint64_t span = static_cast<uint64_t>(last) - first + 1;
      if (span > layout.glyphCount || offset > layout.glyphCount || offset != expectedOffset ||
          span > layout.glyphCount - offset) {
        file.close();
        return false;
      }
      expectedOffset += static_cast<uint32_t>(span);
      previousLast = last;
    }
    if (expectedOffset != layout.glyphCount) {
      file.close();
      return false;
    }
    esp_task_wdt_reset();
  }
  file.close();

  return true;
}

bool FontInstaller::buildFontPath(const char* family, const char* filename, char* outBuf, size_t outBufSize) {
  // Use the same root selection as ensureFamilyDir: existing install dir wins,
  // otherwise the default-write root.
  const char* root = SdCardFontRegistry::findFamilyRoot(family);
  if (!root) root = SdCardFontRegistry::defaultWriteRoot();
  const int written = snprintf(outBuf, outBufSize, "%s/%s/%s", root, family, filename);
  return written > 0 && static_cast<size_t>(written) < outBufSize &&
         font_upload::pathLengthIsSafe(static_cast<size_t>(written));
}

FontInstaller::Error FontInstaller::deleteFamily(const char* familyName) {
  if (!isValidFamilyName(familyName)) {
    return Error::INVALID_FAMILY_NAME;
  }

  // A family may exist in either root (or, edge case, both). Remove from both.
  const char* roots[] = {SdCardFontRegistry::FONTS_DIR_HIDDEN, SdCardFontRegistry::FONTS_DIR_VISIBLE};
  bool removedAny = false;
  bool sawAny = false;
  for (const char* root : roots) {
    char dirPath[160];
    snprintf(dirPath, sizeof(dirPath), "%s/%s", root, familyName);
    if (!Storage.exists(dirPath)) continue;
    sawAny = true;
    if (!Storage.removeDir(dirPath)) {
      LOG_ERR("FONT", "Failed to remove family dir: %s", dirPath);
      return Error::SD_WRITE_ERROR;
    }
    removedAny = true;
  }

  if (!sawAny) {
    LOG_DBG("FONT", "Family not found in any fonts root: %s", familyName);
    return Error::OK;  // Already gone
  }
  (void)removedAny;

  // If this was the active font, clear the setting
  if (strcmp(SETTINGS.sdFontFamilyName, familyName) == 0) {
    SETTINGS.sdFontFamilyName[0] = '\0';
    SETTINGS.saveToFile();
    LOG_DBG("FONT", "Cleared active SD font (deleted family: %s)", familyName);
  }

  return Error::OK;
}

void FontInstaller::refreshRegistry() { registry_.discover(); }

bool FontInstaller::isFamilyInstalled(const char* familyName) const {
  return registry_.findFamily(familyName) != nullptr;
}
