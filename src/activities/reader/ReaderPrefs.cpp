#include "ReaderPrefs.h"

#include <cstring>

#include "CrossPointSettings.h"

ReaderPrefs ReaderPrefs::fromGlobal() {
  ReaderPrefs p;
  p.fontFamily = SETTINGS.fontFamily;
  p.fontSize = SETTINGS.fontSize;
  p.lineSpacingPercent = SETTINGS.lineSpacingPercent;
  p.paragraphAlignment = SETTINGS.paragraphAlignment;
  p.wordSpacing = SETTINGS.wordSpacing;
  p.paragraphSpacing = SETTINGS.paragraphSpacing;
  p.extraParagraphSpacing = SETTINGS.extraParagraphSpacing;
  p.uniformMargins = SETTINGS.uniformMargins;
  p.screenMargin = SETTINGS.screenMargin;
  p.screenMarginTop = SETTINGS.screenMarginTop;
  p.screenMarginBottom = SETTINGS.screenMarginBottom;
  p.firstLineIndentMode = SETTINGS.firstLineIndentMode;
  p.firstLineIndentPercent = SETTINGS.firstLineIndentPercent;
  p.hyphenationEnabled = SETTINGS.hyphenationEnabled;
  p.embeddedStyle = SETTINGS.embeddedStyle;
  p.focusReadingEnabled = SETTINGS.focusReadingEnabled;
  p.guideDotsEnabled = SETTINGS.guideDotsEnabled;
  p.imageRendering = SETTINGS.imageRendering;
  p.orientation = SETTINGS.orientation;
  // Zero-pad then copy so the trailing bytes are canonical — the reader compares
  // whole ReaderPrefs blobs (memcmp) to detect an edit, so stray bytes past the
  // string terminator must not read as a change.
  std::memset(p.sdFontFamilyName, 0, sizeof(p.sdFontFamilyName));
  std::strncpy(p.sdFontFamilyName, SETTINGS.sdFontFamilyName, sizeof(p.sdFontFamilyName) - 1);
  return p;
}

int readerFontId(const ReaderPrefs& p) { return SETTINGS.resolveReaderFontId(p.fontSize, p.sdFontFamilyName); }
