#pragma once

class GfxRenderer;

// Binds the active UI font ids (UI_10_FONT_ID / UI_12_FONT_ID) to the Cozette family
// (default) or the Ubuntu family (Arabic/Hebrew, which Cozette cannot draw) based on
// the current I18n language. Defined in main.cpp alongside the font-family globals.
// Call at boot and immediately after any in-app language change so the menus reflect
// the new language's script without a reboot.
void bindUiFontsForLanguage(GfxRenderer& renderer);
