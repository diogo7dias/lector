#pragma once

#include "components/themes/BaseTheme.h"

class GfxRenderer;
struct StatusBarBlock;

namespace statusbar_preview {

// Draws a live mini-page preview of the v2 status bar (top bar, optional hairline
// divider, sample reading line, bottom bar, and edge progress bars) inside `bounds`.
// Mirrors TextSettingsPreview so the list screen stays free of direct renderer calls.
void renderPreview(GfxRenderer& renderer, const Rect& bounds, const StatusBarBlock& sb, bool atChapterStart);

}  // namespace statusbar_preview
