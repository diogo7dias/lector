#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "GfxRenderer.h"
#include "UiFont.h"
#include "components/OptionPopupLayout.h"
#include "components/themes/BaseTheme.h"
#include "fontIds.h"

// The option pop-up's geometry measured from the live renderer: the wrap, then the layout
// in OptionPopupLayout.h. Shared by the painter (BaseTheme::drawOptionPopup) and the hit
// test in OptionPopup, so a tap always resolves to the row that was drawn.
namespace option_popup {

// Arabic and Hebrew draw everything in their UI face, which Literata cannot stand in for.
inline int rowFont() { return UI_10_FONT_ID; }
inline int headFont() { return uiLanguageNeedsUbuntu() ? UI_10_FONT_ID : LITERATA_UI_26_FONT_ID; }
inline int disabledFont() { return uiLanguageNeedsUbuntu() ? UI_10_FONT_ID : LITERATA_UI_19_IT_FONT_ID; }

inline Geometry compute(const GfxRenderer& renderer, const char* title, const std::vector<std::string>& options) {
  Geometry g;
  const int pageWidth = renderer.getScreenWidth();
  const int textW = std::max(1, pageWidth - (SIDE + FRAME + TEXT_INSET) * 2);
  // wrapUiText measures in the row face; the heading face is wider by its size ratio.
  const int headW = std::max(1, textW * renderer.getLineHeight(rowFont()) / renderer.getLineHeight(headFont()));
  g.titleLines = BaseTheme::wrapUiText(renderer, title != nullptr ? title : "", headW, headW);
  g.optionLines.reserve(options.size());
  for (const auto& opt : options) g.optionLines.push_back(BaseTheme::wrapUiText(renderer, opt, textW, textW));
  place(g, pageWidth, renderer.getScreenHeight(), renderer.getLineHeight(rowFont()));
  return g;
}

}  // namespace option_popup
