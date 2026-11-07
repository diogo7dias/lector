#pragma once

class GfxRenderer;

// Binds the active UI font ids (UI_10_FONT_ID / UI_12_FONT_ID) to the Cozette family
// (default) or the Ubuntu family (Arabic/Hebrew, which Cozette cannot draw) based on
// the current I18n language.
// Call at boot and immediately after any in-app language change so the menus reflect
// the new language's script without a reboot.
void bindUiFontsForLanguage(GfxRenderer& renderer);

// Registers the compiled-in families (ChareInk reading font, Ubuntu and Cozette UI) and
// binds the UI ids for the current language. SD card families load separately.
void bindBuiltinFonts(GfxRenderer& renderer);
