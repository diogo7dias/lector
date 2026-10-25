#pragma once

class GfxRenderer;

// The plain lector face: the crow above the name, centred as one block. The boot screen
// paints it when there is no wallpaper to redraw.
// Draws only; the caller clears the screen first and submits after.
// The caller has the renderer in Portrait: drawIcon only plots that orientation.
void drawLectorFace(const GfxRenderer& renderer);
