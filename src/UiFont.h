#pragma once

class GfxRenderer;

// Registers the compiled-in families (ChareInk reading font, Cozette UI and the paragraph
// number digits). SD card families load separately.
void bindBuiltinFonts(GfxRenderer& renderer);
