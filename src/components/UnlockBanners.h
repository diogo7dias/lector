#pragma once

#include <string>

class GfxRenderer;

// Override the book title the banner names, for boots that are about to open a book
// other than APP_STATE.openEpubPath — today only "Open Book on Boot", which
// picks its target after the banner would otherwise have painted the previous book.
// Set once during setup(), before the first drawUnlockBanners() call; an empty path
// means "use APP_STATE.openEpubPath" (the normal resume case).
//
// A module-level string rather than a drawUnlockBanners() parameter: BootActivity
// passes the function as a plain `void(GfxRenderer&)` callback, so the signature is
// fixed. It is boot-only and holds one short path, so the DRAM cost is a few dozen
// bytes that are never touched again after the reader paints.
void setUnlockBannerBookPath(const std::string& path);

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

// The TOP banner only (version + the book being resumed), without the footer band.
// Drawn by the Light sleep face, which names the book the wake is about to open.
void drawUnlockBannerTop(GfxRenderer& renderer);
