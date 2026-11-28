#include "LightPanel.h"

#include <Arduino.h>

#include <cstdio>
#include <string>

void LightPanel::render(const GfxRenderer& renderer) const {  // NOLINT: caches layout for touch
  if (!active_) return;

  const int screenWidth = renderer.getScreenWidth();
  const int lineHeight = renderer.getLineHeight(banner::FONT_ID);
  layout_ = light_panel::forScreen(screenWidth, lineHeight, renderer.getLineHeight(UI_10_FONT_ID),
                                   Frontlight.hasColorTemperature(), context_.hasAux(), context_.actionCount,
                                   Frontlight.present());

  // Physical top crop (X4 crops ~9px, X3 none): the backing reaches the physical
  // edge while the rows sit below the crop, matching message popups.
  int viewTop = 0;
  int viewRight = 0;
  int viewBottom = 0;
  int viewLeft = 0;
  renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &viewLeft);
  topInset_ = viewTop;

  const int bandHeight = layout_.height + topInset_;
  light_panel::stipplePageBelow(renderer, bandHeight);

  renderer.fillRect(0, 0, screenWidth, bandHeight, false);
  renderer.fillRect(0, bandHeight - banner::RULE, screenWidth, banner::RULE, true);

  // The in-book menu's look without its rules: full-width rows, the selected row filled
  // black, the steppers drawn as its boxed minus and plus.
  if (layout_.hasLight) drawToggleRow(renderer);
  drawSliderRow(renderer, layout_.brightness, Sun24Icon, brightness_);
  if (layout_.hasWarmth) drawSliderRow(renderer, layout_.warmth, Thermometer24Icon, warmth_);
  if (layout_.hasAux) drawAuxRow(renderer, layout_.aux, context_.auxText);

  for (int i = 0; i < layout_.actionCount; ++i) {
    char fadeLabel[24];
    const uint8_t action = context_.actions[i];
    const char* label = (action == CrossPointSettings::LP_MENU_CALM_MODE && SETTINGS.calmMode)
                            ? I18N.get(StrId::STR_CALM_MODE_ON)
                            : I18N.get(boundMenuActionLabel(action));
    if (action == CrossPointSettings::LP_MENU_CANDLE_FADE && frontlight::candleFadeActive()) {
      snprintf(fadeLabel, sizeof(fadeLabel), "%s %um", label,
               static_cast<unsigned>(frontlight::candleFadeRemainingMinutes(millis())));
      label = fadeLabel;
    }
    drawBox(renderer, layout_.actions[i], label, cursor_.action == i);
  }
  light_panel::drawReadout(renderer, layout_.readout, topInset_, freeBytes_);
}

// Frontlight as a menu row: the name at the left, On or Off at the right.
void LightPanel::drawToggleRow(const GfxRenderer& renderer) const {
  const auto& rect = layout_.toggle;
  const int textY = rect.y + topInset_ + (rect.height - renderer.getLineHeight(banner::FONT_ID)) / 2;
  renderer.drawText(banner::FONT_ID, rect.x, textY, I18N.get(StrId::STR_FRONTLIGHT), true);
  const char* state = I18N.get(on_ ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
  renderer.drawText(banner::FONT_ID, rect.x + rect.width - renderer.getTextWidth(banner::FONT_ID, state), textY, state,
                    true);
}

// A framed box with its label centred; filled black with white text under the key cursor.
void LightPanel::drawBox(const GfxRenderer& renderer, const light_panel::Rect& rect, const char* label,
                         const bool selected) const {
  const int y = rect.y + topInset_;
  if (selected) {
    renderer.fillRect(rect.x, y, rect.width, rect.height, true);
  } else {
    renderer.drawRect(rect.x, y, rect.width, rect.height, light_panel::kRule, true);
  }
  // A label wider than the box (German "Bildschirm regenerieren", French "Reprendre la lecture")
  // ran past both sides of the frame. It ends in an ellipsis instead: two lines of the UI
  // face (24 px of ink each, descender included) do not fit the 45 px box without one
  // line's descender crossing the bottom rule.
  constexpr int kLabelPad = 4;
  const std::string shown = renderer.truncatedText(banner::FONT_ID, label, rect.width - 2 * kLabelPad);
  const int textWidth = renderer.getTextWidth(banner::FONT_ID, shown.c_str());
  const int textY = y + (rect.height - renderer.getLineHeight(banner::FONT_ID)) / 2;
  renderer.drawText(banner::FONT_ID, rect.x + (rect.width - textWidth) / 2, textY, shown.c_str(), !selected);
}

// Icon, track, number, then the two steppers. The row is named by its icon rather than
// by a word: the word and the track were fighting for the same 44 px, and the icon says
// which value this is in a third of the space.
void LightPanel::drawSliderRow(const GfxRenderer& renderer, const light_panel::StepRow& row, const freeink::Icon& icon,
                               const uint8_t value) const {
  if (row.height == 0) return;
  const bool ink = !drawSelection(renderer, row);
  drawSteppers(renderer, row, ink);

  const int rowCenter = row.y + topInset_ + row.height / 2;
  renderer.drawIcon(icon.bits, row.icon.x, rowCenter - icon.opticalCenterY, icon.w, ink);

  slider_look::drawTrack(renderer, row.bar.x, row.bar.y + topInset_, row.bar.width, row.bar.height, value,
                         maxFor(row.row), ink);

  char number[8];
  snprintf(number, sizeof(number), "%d", static_cast<int>(value));
  drawCentered(renderer, row.value, number, /*center=*/false, ink);
}

// No icon and no track: the label owns the run up to the stepper column.
void LightPanel::drawAuxRow(const GfxRenderer& renderer, const light_panel::StepRow& row, const char* label) const {
  if (row.height == 0) return;
  const bool ink = !drawSelection(renderer, row);
  drawSteppers(renderer, row, ink);
  drawCentered(renderer, row.value, label, /*center=*/true, ink);
}

void LightPanel::drawSteppers(const GfxRenderer& renderer, const light_panel::StepRow& row, const bool ink) const {
  slider_look::drawStepper(renderer, row.minus.x, row.minus.y + topInset_, row.minus.width, row.minus.height,
                           /*plus=*/false, ink);
  slider_look::drawStepper(renderer, row.plus.x, row.plus.y + topInset_, row.plus.width, row.plus.height,
                           /*plus=*/true, ink);
}

// Vertically centred in `rect` either way; `center` picks horizontal centring over
// starting at the left edge.
void LightPanel::drawCentered(const GfxRenderer& renderer, const light_panel::Rect& rect, const char* text,
                              const bool center, const bool ink) const {
  const int textY = rect.y + topInset_ + (rect.height - renderer.getLineHeight(banner::FONT_ID)) / 2;
  const int textX = center ? rect.x + (rect.width - renderer.getTextWidth(banner::FONT_ID, text)) / 2 : rect.x;
  renderer.drawText(banner::FONT_ID, textX, textY, text, ink);
}

// Button users need to see which row Left/Right will move: it fills black edge to edge,
// like the in-book menu's selected row, and the row is then drawn in white. True when
// the row is selected.
bool LightPanel::drawSelection(const GfxRenderer& renderer, const light_panel::StepRow& row) const {
  if (cursor_.onGrid() || cursor_.row != row.row) return false;
  renderer.fillRect(0, row.y + topInset_, layout_.width, row.height, true);
  return true;
}
