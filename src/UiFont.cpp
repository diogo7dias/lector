#include "UiFont.h"

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>

#include "fontIds.h"

namespace {
// Fonts
// ChareInk is lector's single built-in reading family (an e-ink tuned Charis derivative).
// It is compiled in at 12, 14 and 16 pt (BUILTIN_READER_POINT_SIZES): its glyph set is far
// larger than the Vollkorn it replaced, so 18 pt and above stay SD card fonts, like every
// other family. Cozette is the UI font.
EpdFont chareink12RegularFont(&chareink_12_regular);
EpdFont chareink12BoldFont(&chareink_12_bold);
EpdFont chareink12ItalicFont(&chareink_12_italic);
EpdFont chareink12BoldItalicFont(&chareink_12_bolditalic);
EpdFontFamily chareink12FontFamily(12, &chareink12RegularFont, &chareink12BoldFont, &chareink12ItalicFont,
                                   &chareink12BoldItalicFont);

EpdFont chareink14RegularFont(&chareink_14_regular);
EpdFont chareink14BoldFont(&chareink_14_bold);
EpdFont chareink14ItalicFont(&chareink_14_italic);
EpdFont chareink14BoldItalicFont(&chareink_14_bolditalic);
EpdFontFamily chareink14FontFamily(14, &chareink14RegularFont, &chareink14BoldFont, &chareink14ItalicFont,
                                   &chareink14BoldItalicFont);

EpdFont chareink16RegularFont(&chareink_16_regular);
EpdFont chareink16BoldFont(&chareink_16_bold);
EpdFont chareink16ItalicFont(&chareink_16_italic);
EpdFont chareink16BoldItalicFont(&chareink_16_bolditalic);
EpdFontFamily chareink16FontFamily(16, &chareink16RegularFont, &chareink16BoldFont, &chareink16ItalicFont,
                                   &chareink16BoldItalicFont);

// The UI families ship REGULAR ONLY, and the regular face fills the family's bold slot
// so a stray BOLD request resolves to regular instead of nullptr. This is the old-Lector
// arrangement: menu weight hierarchy comes from SIZE, not from a second cut. Emphasis in
// the UI, if ever wanted, is the Paperback Look smear (GfxRenderer::setPaperbackLook),
// which thickens whatever face is loaded and costs no flash.

// Cozette UI family — lector's menu font (Latin + Cyrillic + Greek + Vietnamese, so book
// titles in those scripts draw in the lists). Sizes match the previous mature Lector:
// 10 = SMALL_FONT_ID, 12 = UI_10_FONT_ID (list rows), 14 = UI_12_FONT_ID (header title).
EpdFont cozette10RegularFont(&cozette_10_regular);
EpdFontFamily cozette10FontFamily(10, &cozette10RegularFont, &cozette10RegularFont);

EpdFont cozette12RegularFont(&cozette_12_regular);
EpdFontFamily cozette12FontFamily(12, &cozette12RegularFont, &cozette12RegularFont);

EpdFont cozette14RegularFont(&cozette_14_regular);
EpdFontFamily cozette14FontFamily(14, &cozette14RegularFont, &cozette14RegularFont);

// Paragraph numbers: Cozette's ten digits at 1x, 2x and 3x its 13px grid. A pixel face
// only stays crisp on whole multiples of its grid, and digits alone cost under 1 KB. Sized in
// pixels, so their families carry no point size (0).
EpdFont paraNum13Font(&cozette_digits_13);
EpdFontFamily paraNum13FontFamily(0, &paraNum13Font, &paraNum13Font);
EpdFont paraNum26Font(&cozette_digits_26);
EpdFontFamily paraNum26FontFamily(0, &paraNum26Font, &paraNum26Font);
EpdFont paraNum39Font(&cozette_digits_39);
EpdFontFamily paraNum39FontFamily(0, &paraNum39Font, &paraNum39Font);

}  // namespace

void bindBuiltinFonts(GfxRenderer& renderer) {
  renderer.insertFont(CHAREINK_12_FONT_ID, chareink12FontFamily);
  renderer.insertFont(CHAREINK_14_FONT_ID, chareink14FontFamily);
  renderer.insertFont(CHAREINK_16_FONT_ID, chareink16FontFamily);
  renderer.insertFont(PARA_NUM_13_FONT_ID, paraNum13FontFamily);
  renderer.insertFont(PARA_NUM_26_FONT_ID, paraNum26FontFamily);
  renderer.insertFont(PARA_NUM_39_FONT_ID, paraNum39FontFamily);
  // UI ids, 2px larger than the CrossPoint base: SMALL = 10, UI_10 (list rows) = 12,
  // UI_12 (header title) = 14.
  renderer.insertFont(SMALL_FONT_ID, cozette10FontFamily);
  renderer.insertFont(UI_10_FONT_ID, cozette12FontFamily);
  renderer.insertFont(UI_12_FONT_ID, cozette14FontFamily);
}
