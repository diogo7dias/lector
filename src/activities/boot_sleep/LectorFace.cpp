#include "LectorFace.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "components/icons/crow120.h"
#include "fontIds.h"

void drawLectorFace(const GfxRenderer& renderer) {
  constexpr int GAP = 16;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int top = (renderer.getScreenHeight() - Crow120Icon.h - GAP - lineHeight) / 2;
  renderer.drawIcon(Crow120Icon.bits, (renderer.getScreenWidth() - Crow120Icon.w) / 2, top, Crow120Icon.w);
  renderer.drawCenteredText(UI_10_FONT_ID, top + Crow120Icon.h + GAP, tr(STR_LECTOR));
}
