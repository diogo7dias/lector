#include "ReaderPrefs.h"

#include <HalStorage.h>

#include <cstring>

#include "CrossPointSettings.h"

ReaderPrefs ReaderPrefs::fromGlobal() {
  ReaderPrefs p;
  p.fontFamily = SETTINGS.fontFamily;
  p.fontPointSize = SETTINGS.fontPointSize;
  p.lineSpacingPercent = SETTINGS.lineSpacingPercent;
  p.paragraphAlignment = SETTINGS.paragraphAlignment;
  p.extraParagraphSpacing = SETTINGS.extraParagraphSpacing;
  p.paragraphSpacing = SETTINGS.paragraphSpacing;
  p.screenMargin = SETTINGS.screenMargin;
  p.screenMarginTop = SETTINGS.screenMarginTop;
  p.screenMarginBottom = SETTINGS.screenMarginBottom;
  p.uniformMargins = SETTINGS.uniformMargins;
  p.dynamicMargins = SETTINGS.dynamicMargins;
  p.focusReadingEnabled = SETTINGS.focusReadingEnabled;
  p.guideDotsEnabled = SETTINGS.guideDotsEnabled;
  p.hyphenationEnabled = SETTINGS.hyphenationEnabled;
  p.embeddedStyle = SETTINGS.embeddedStyle;
  p.textAntiAliasing = SETTINGS.textAntiAliasing;
  p.imageRendering = SETTINGS.imageRendering;
  p.paperbackLookBody = SETTINGS.paperbackLookBody;
  p.paperbackLookStatus = SETTINGS.paperbackLookStatus;
  p.firstLineIndentMode = SETTINGS.firstLineIndentMode;
  p.firstLineIndentPercent = SETTINGS.firstLineIndentPercent;
  p.paragraphNumbering = SETTINGS.paragraphNumbering;
  p.paragraphNumberSize = SETTINGS.paragraphNumberSize;
  p.statusBarEnabled = SETTINGS.sbEnabled;
  // Zero-pad then copy so the trailing bytes are canonical for whole-blob memcmp.
  std::memset(p.sdFontFamilyName, 0, sizeof(p.sdFontFamilyName));
  std::strncpy(p.sdFontFamilyName, SETTINGS.sdFontFamilyName, sizeof(p.sdFontFamilyName) - 1);
  return p;
}

// Device (SD) serialization. Byte counts are checked so a truncated write/read
// fails cleanly (build is -fno-exceptions; a short read must never be treated as
// a valid record). HalFile::write returns bytes written; HalFile::read returns
// bytes read (int).
bool writeReaderPrefs(HalFile& out, const ReaderPrefs& p) {
  const uint8_t ver = ReaderPrefs::VERSION;
  if (out.write(&ver, 1) != 1) return false;
  return out.write(reinterpret_cast<const uint8_t*>(&p), sizeof(ReaderPrefs)) == sizeof(ReaderPrefs);
}

bool readReaderPrefs(HalFile& in, ReaderPrefs& p, bool* migrated) {
  if (migrated) *migrated = false;
  uint8_t ver = 0;
  if (in.read(&ver, 1) != 1) return false;
  // v5 through v9 are read and upgraded rather than discarded, which would silently drop
  // every per-book override the first time this build runs. Each older layout is a strict
  // prefix of the current struct, so a record is read at its own shorter length and every
  // field appended since keeps its constructed default.
  const size_t want = readerPrefsRecordSize(ver);
  if (want == 0) return false;
  ReaderPrefs tmp;
  if (in.read(reinterpret_cast<uint8_t*>(&tmp), want) != static_cast<int>(want)) {
    return false;
  }
  if (ver == 5) tmp.fontPointSize = foldLegacyReaderFontSize(tmp.fontPointSize);
  if (ver < ReaderPrefs::FIRST_VERSION_WITH_CURRENT_DEFAULTS) tmp.adoptCurrentReadingDefaults();
  if (ver < ReaderPrefs::VERSION && migrated) *migrated = true;
  p = tmp;
  return true;
}
