#pragma once

class GfxRenderer;

// Draws the wake/unlock screen's two framed banners over whatever is already on the
// framebuffer (the restored sleep wallpaper): TOP = "Lector <version>" plus the
// resuming book title (uppercased, wrapped to at most 12 rows); BOTTOM = the user's
// custom footer text (SETTINGS.customFooter, default "READ UNTIL YOU DIE."). Restored
// from old lector.
//
// The black banner fill is drawn in every render pass; the white inset border + text
// draw only in the BW base pass (the 1-bit draw path would otherwise set grayscale
// plane bits in the LSB/MSB passes and grey the white pixels).
void drawUnlockBanners(GfxRenderer& renderer);
