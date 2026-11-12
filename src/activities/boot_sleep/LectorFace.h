#pragma once

class GfxRenderer;

// The plain lector face: the crow above the name, centred as one block. Boot and the
// sleep fallback paint the same face, so a wake that shows it changes nothing on the panel.
// Draws only; the caller clears the screen first and submits after.
// The caller has the renderer in Portrait: drawIcon only plots that orientation.
void drawLectorFace(const GfxRenderer& renderer);
