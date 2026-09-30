#pragma once

#include "UiFont.h"
#include "components/UiAppHost.h"
#include "fontIds.h"

// The contents look's faces and list props, shared by the list and status bases so a
// row reads the same wherever it is drawn.
namespace contents_look {

// Heading, numeral and italic-line faces into the extra slots. Arabic and Hebrew draw
// everything in their UI face, which Literata cannot stand in for.
inline void bindFonts(freeink::ui::GfxRendererTarget& target) {
  using freeink::ui::GfxRendererTarget;
  const bool ubuntu = uiLanguageNeedsUbuntu();
  target.setFont(GfxRendererTarget::FONT_EXTRA_1, ubuntu ? UI_10_FONT_ID : LITERATA_UI_26_FONT_ID);
  target.setFont(GfxRendererTarget::FONT_EXTRA_2, ubuntu ? UI_10_FONT_ID : LITERATA_UI_16_FONT_ID);
  target.setFont(GfxRendererTarget::FONT_EXTRA_3, ubuntu ? UI_10_FONT_ID : LITERATA_UI_19_IT_FONT_ID);
}

// Rows in the body face, headings and numerals and italic lines in the bound extras.
inline void applyListProps(freeink::ui::ListProps& props) {
  using freeink::ui::GfxRendererTarget;
  props.contentsLook = true;
  props.labelText.font = GfxRendererTarget::FONT_BODY;
  props.valueText.font = GfxRendererTarget::FONT_BODY;
  props.headerText.font = GfxRendererTarget::FONT_EXTRA_1;
  props.headingNumeralText.font = GfxRendererTarget::FONT_EXTRA_2;
  props.subtitleText.font = GfxRendererTarget::FONT_EXTRA_3;
}

// One row's height in the contents look, for the nav's page estimate.
inline int16_t rowHeight(const bool hasSubtitle) {
  return hasSubtitle ? freeink::ui::contents::SUB_ROW_H : freeink::ui::contents::ROW_H;
}

}  // namespace contents_look
