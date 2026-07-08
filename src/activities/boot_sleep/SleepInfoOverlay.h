#pragma once

#include <string>

class GfxRenderer;

// Draws the optional sleep-screen info overlay in the bottom-left corner:
//  - showSleepImageFilename ON  -> the wallpaper's (favorite-suffix-stripped)
//    filename in a filled box.
//  - else showSleepFavoriteBadge ON and the file is a favorite -> a small "F" box.
// No-op when neither applies or the path is empty. Must be called once per
// grayscale pass (BW base + LSB + MSB) so it composites solid, mirroring how the
// wallpaper itself is redrawn per pass.
void drawSleepInfoOverlay(const GfxRenderer& renderer, const std::string& sourcePath);
