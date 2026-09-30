#pragma once

class GfxRenderer;

// True for the UI languages Literata cannot draw (Arabic, Hebrew): those keep the
// Ubuntu family, and screens that pick Literata faces by id fall back to UI_10_FONT_ID.
bool uiLanguageNeedsUbuntu();

// Binds the active UI font ids (UI_10_FONT_ID / UI_12_FONT_ID) to Literata
// (default) or the Ubuntu family (Arabic/Hebrew, which Literata cannot draw) based on
// the current I18n language. Defined in main.cpp alongside the font-family globals.
// Call at boot and immediately after any in-app language change so the menus reflect
// the new language's script without a reboot.
void bindUiFontsForLanguage(GfxRenderer& renderer);
