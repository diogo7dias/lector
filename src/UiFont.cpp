#include "UiFont.h"

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <builtinFonts/all.h>

#include "fontIds.h"

namespace {
// Fonts
// ChareInk is lector's single built-in reading family (an e-ink tuned Charis derivative).
// It is compiled in at 14 pt only: its glyph set is far larger than the Vollkorn it
// replaced, and four sizes would not fit the app partition. Every other size, and every
// other family, is installed from the SD card. Ubuntu and Cozette are the UI fonts.
EpdFont chareink14RegularFont(&chareink_14_regular);
EpdFont chareink14BoldFont(&chareink_14_bold);
EpdFont chareink14ItalicFont(&chareink_14_italic);
EpdFont chareink14BoldItalicFont(&chareink_14_bolditalic);
EpdFontFamily chareink14FontFamily(&chareink14RegularFont, &chareink14BoldFont, &chareink14ItalicFont,
                                   &chareink14BoldItalicFont);

// The UI families ship REGULAR ONLY, and the regular face fills the family's bold slot
// so a stray BOLD request resolves to regular instead of nullptr. This is the old-Lector
// arrangement: menu weight hierarchy comes from SIZE, not from a second cut. Emphasis in
// the UI, if ever wanted, is the Paperback Look smear (GfxRenderer::setPaperbackLook),
// which thickens whatever face is loaded and costs no flash.

// Ubuntu UI family — the FULL-coverage fallback (Latin + Arabic + Hebrew + Vietnamese,
// baked with the extra script intervals). Bound permanently to UBUNTU_10/12_FONT_ID and
// used for Arabic/Hebrew UI and for the language-picker native-name list.
EpdFont ubuntu10RegularFont(&ubuntu_10_regular);
EpdFontFamily ubuntu10FontFamily(&ubuntu10RegularFont, &ubuntu10RegularFont);

EpdFont ubuntu12RegularFont(&ubuntu_12_regular);
EpdFontFamily ubuntu12FontFamily(&ubuntu12RegularFont, &ubuntu12RegularFont);

EpdFont ubuntu14RegularFont(&ubuntu_14_regular);
EpdFontFamily ubuntu14FontFamily(&ubuntu14RegularFont, &ubuntu14RegularFont);

// Cozette UI family — lector's default menu font (Latin + Cyrillic + Greek + Vietnamese;
// no Arabic/Hebrew). Sizes match the previous mature Lector: 10 = SMALL_FONT_ID,
// 12 = UI_10_FONT_ID (list rows), 14 = UI_12_FONT_ID (header title). Bound for every
// language except Arabic/Hebrew (which use the Ubuntu family at the same sizes).
EpdFont cozette10RegularFont(&cozette_10_regular);
EpdFontFamily cozette10FontFamily(&cozette10RegularFont, &cozette10RegularFont);

EpdFont cozette12RegularFont(&cozette_12_regular);
EpdFontFamily cozette12FontFamily(&cozette12RegularFont, &cozette12RegularFont);

EpdFont cozette14RegularFont(&cozette_14_regular);
EpdFontFamily cozette14FontFamily(&cozette14RegularFont, &cozette14RegularFont);

// Paragraph numbers: Cozette's ten digits at 1x, 2x and 3x its 13px grid. A pixel face
// only stays crisp on whole multiples of its grid, and digits alone cost under 1 KB.
EpdFont paraNum13Font(&cozette_digits_13);
EpdFontFamily paraNum13FontFamily(&paraNum13Font, &paraNum13Font);
EpdFont paraNum26Font(&cozette_digits_26);
EpdFontFamily paraNum26FontFamily(&paraNum26Font, &paraNum26Font);
EpdFont paraNum39Font(&cozette_digits_39);
EpdFontFamily paraNum39FontFamily(&paraNum39Font, &paraNum39Font);

// Cozette cannot draw Arabic or Hebrew, so those two UI languages use the Ubuntu
// family. Every other language (incl. Cyrillic + Vietnamese, verified in Cozette's
// cmap) uses Cozette. Called at boot and on every in-app language change (declared
// in UiFont.h so LanguageSelectActivity can rebind after a change).
bool uiLanguageNeedsUbuntu() {
  const Language lang = I18n::getInstance().getLanguage();
  return lang == Language::AR || lang == Language::HE;
}

}  // namespace

void bindUiFontsForLanguage(GfxRenderer& renderer) {
  const bool useUbuntu = uiLanguageNeedsUbuntu();
  // insertFont() ignores an already-registered id, so drop the old binding first.
  // Sizes mirror the previous mature Lector (2px larger than the CrossPoint base):
  // SMALL = 10, UI_10 (list rows) = 12, UI_12 (header title) = 14. Arabic/Hebrew use the
  // Ubuntu family at the same sizes so their small text renders too (Cozette lacks AR/HE).
  renderer.removeFont(SMALL_FONT_ID);
  renderer.removeFont(UI_10_FONT_ID);
  renderer.removeFont(UI_12_FONT_ID);
  renderer.insertFont(SMALL_FONT_ID, useUbuntu ? ubuntu10FontFamily : cozette10FontFamily);
  renderer.insertFont(UI_10_FONT_ID, useUbuntu ? ubuntu12FontFamily : cozette12FontFamily);
  renderer.insertFont(UI_12_FONT_ID, useUbuntu ? ubuntu14FontFamily : cozette14FontFamily);
}

void bindBuiltinFonts(GfxRenderer& renderer) {
  renderer.insertFont(CHAREINK_14_FONT_ID, chareink14FontFamily);
  renderer.insertFont(PARA_NUM_13_FONT_ID, paraNum13FontFamily);
  renderer.insertFont(PARA_NUM_26_FONT_ID, paraNum26FontFamily);
  renderer.insertFont(PARA_NUM_39_FONT_ID, paraNum39FontFamily);
  // Permanent Ubuntu ids (full Latin/Arabic/Hebrew/Vietnamese coverage) for the
  // language-select native-name list and the Arabic/Hebrew UI.
  renderer.insertFont(UBUNTU_10_FONT_ID, ubuntu10FontFamily);
  renderer.insertFont(UBUNTU_12_FONT_ID, ubuntu12FontFamily);
  // Active UI ids (SMALL / UI_10 / UI_12): Cozette by default, Ubuntu for Arabic/Hebrew.
  bindUiFontsForLanguage(renderer);
}
