#pragma once

#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <functional>

#include "BannerStyle.h"
#include "CrossPointSettings.h"
#include "LightPanelGeometry.h"
#include "MappedInputManager.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"

// The light panel: a black band pulled down from the top edge holding the frontlight's
// three controls. Drawn over whatever is on screen without clearing it, the same way
// OptionPopup and ValueBarPopup are, so the page you were reading is still there to
// judge the light against — which is the whole reason to reach for it mid-page.
//
// Every change applies to the hardware immediately; SETTINGS is written once, on close,
// so a drag across the bar does not spend a file write per pixel.
class LightPanel {
 public:
  void show() {
    if (!Frontlight.present()) return;
    on_ = Frontlight.isOn();
    brightness_ = Frontlight.brightness();
    warmth_ = Frontlight.warmth();
    selected_ = light_panel::Row::Brightness;
    active_ = true;
  }

  bool isActive() const { return active_; }

  bool handleInput(MappedInputManager& input, const std::function<void()>& requestUpdate) {
    if (!active_) return false;

    // A second top swipe puts the panel away: the gesture that opened it is the one
    // the hand is already making.
    if (input.wasMenuGesture() || input.wasPressed(MappedInputManager::Button::Back) ||
        input.wasPressed(MappedInputManager::Button::Confirm)) {
      close(requestUpdate);
      return true;
    }

    int tx = 0;
    int ty = 0;
    const bool down = input.wasScreenTouchDown(tx, ty);
    const bool held = !down && input.isScreenTouchHeld(tx, ty);
    if (down || held) {
      const int localY = ty - topInset_;
      if (!light_panel::insidePanel(layout_, tx, localY)) {
        // A tap on the page below is "put it away", not a page turn: the host returns
        // early on a consumed input, so the tap cannot also reach the reader.
        if (down) close(requestUpdate);
        return true;
      }
      const auto row = light_panel::rowAt(layout_, tx, localY);
      if (row == light_panel::Row::Toggle) {
        if (down) {
          on_ = !on_;
          applyLight();
          requestUpdate();
        }
        return true;
      }
      if (row == light_panel::Row::Brightness || row == light_panel::Row::Warmth) {
        selected_ = row;
        const auto& bar = row == light_panel::Row::Brightness ? layout_.brightness.bar : layout_.warmth.bar;
        const int next = static_cast<uint8_t>(light_panel::valueForX(bar, tx, 0, 100));
        if (setValue(row, next)) requestUpdate();
      }
      return true;
    }

    // Buttons: Up/Down pick the row, Left/Right step it by 1. Same small step the
    // numeric settings rows use, so the two feel like one control.
    nav_.onPressAndContinuous({MappedInputManager::Button::Up},
                              [this, &requestUpdate] { moveSelection(-1, requestUpdate); });
    nav_.onPressAndContinuous({MappedInputManager::Button::Down},
                              [this, &requestUpdate] { moveSelection(1, requestUpdate); });
    nav_.onPressAndContinuous({MappedInputManager::Button::Left},
                              [this, &requestUpdate] { adjustBy(-1, requestUpdate); });
    nav_.onPressAndContinuous({MappedInputManager::Button::Right},
                              [this, &requestUpdate] { adjustBy(1, requestUpdate); });
    return true;
  }

  // Records the layout it drew, so handleInput hit-tests exactly what is on screen.
  void render(const GfxRenderer& renderer) const {  // NOLINT: caches layout for touch
    if (!active_) return;

    const int screenWidth = renderer.getScreenWidth();
    const int lineHeight = renderer.getLineHeight(banner::FONT_ID);
    layout_ = light_panel::forScreen(screenWidth, lineHeight, Frontlight.hasColorTemperature());

    // Physical top crop (X4 crops ~9px, X3 none): the black backing reaches the physical
    // edge while the rows sit below the crop, the same trick the unlock banners use.
    int viewTop = 0;
    int viewRight = 0;
    int viewBottom = 0;
    int viewLeft = 0;
    renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &viewLeft);
    topInset_ = viewTop;

    const int bandHeight = layout_.height + topInset_;
    renderer.fillRect(0, 0, screenWidth, bandHeight, true);
    renderer.fillRect(0, bandHeight - banner::RULE, screenWidth, banner::RULE, false);

    drawToggleRow(renderer);
    drawBarRow(renderer, layout_.brightness, I18N.get(StrId::STR_FRONTLIGHT_BRIGHTNESS), brightness_);
    if (layout_.hasWarmth) {
      drawBarRow(renderer, layout_.warmth, I18N.get(StrId::STR_FRONTLIGHT_WARMTH), warmth_);
    }
  }

  bool processRender(GfxRenderer& renderer) const {
    if (!active_) return false;
    render(renderer);
    renderer.displayBuffer();
    return true;
  }

 private:
  void close(const std::function<void()>& requestUpdate) {
    active_ = false;
    SETTINGS.frontlightOn = on_ ? 1 : 0;
    SETTINGS.frontlightBrightness = brightness_;
    SETTINGS.frontlightWarmth = warmth_;
    SETTINGS.saveToFile();
    requestUpdate();
  }

  void applyLight() {
    Frontlight.setOn(on_);
    if (on_) {
      Frontlight.setBrightness(brightness_);
      Frontlight.setWarmth(warmth_);
    }
  }

  // Moving either bar off zero turns the light back on: the user dragged brightness up,
  // so asking them to also find the toggle would be a puzzle, not a control.
  bool setValue(const light_panel::Row row, const int raw) {
    const auto next = static_cast<uint8_t>(std::clamp(raw, 0, 100));
    uint8_t& target = row == light_panel::Row::Warmth ? warmth_ : brightness_;
    if (target == next) return false;
    target = next;
    if (row == light_panel::Row::Brightness && next > 0) on_ = true;
    applyLight();
    return true;
  }

  void moveSelection(const int delta, const std::function<void()>& requestUpdate) {
    const bool warm = Frontlight.hasColorTemperature();
    const light_panel::Row order[3] = {light_panel::Row::Toggle, light_panel::Row::Brightness,
                                       light_panel::Row::Warmth};
    const int count = warm ? 3 : 2;
    int index = 0;
    for (int i = 0; i < count; ++i) {
      if (order[i] == selected_) index = i;
    }
    selected_ = order[std::clamp(index + delta, 0, count - 1)];
    requestUpdate();
  }

  void adjustBy(const int delta, const std::function<void()>& requestUpdate) {
    if (selected_ == light_panel::Row::Toggle) {
      on_ = !on_;
      applyLight();
      requestUpdate();
      return;
    }
    const uint8_t current = selected_ == light_panel::Row::Warmth ? warmth_ : brightness_;
    if (setValue(selected_, current + delta)) requestUpdate();
  }

  void drawToggleRow(const GfxRenderer& renderer) const {
    const int y = layout_.toggle.y + topInset_;
    const bool picked = selected_ == light_panel::Row::Toggle;
    drawSelection(renderer, layout_.toggle, picked);
    renderer.drawText(banner::FONT_ID, light_panel::kSidePad, y, I18N.get(StrId::STR_FRONTLIGHT), false);
    const char* state = I18N.get(on_ ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
    renderer.drawText(banner::FONT_ID, light_panel::kSidePad + light_panel::kLabelWidth, y, state, false);
  }

  void drawBarRow(const GfxRenderer& renderer, const light_panel::RowLayout& row, const char* label,
                  const uint8_t value) const {
    const int y = row.y + topInset_;
    drawSelection(renderer, row, selected_ == row.row);
    char text[48];
    snprintf(text, sizeof(text), "%s %d", label, static_cast<int>(value));
    renderer.drawText(banner::FONT_ID, light_panel::kSidePad, y, text, false);

    const auto& bar = row.bar;
    const int barY = bar.y + topInset_;
    renderer.drawRect(bar.x, barY, bar.width, bar.height, false);
    const int fill = bar.width * value / 100;
    if (fill > 0) renderer.fillRect(bar.x, barY, fill, bar.height, false);
  }

  // Button users need to see which row Left/Right will move. A white rule under the row
  // rather than a box: the band is already black, and a full frame fought with the bar.
  void drawSelection(const GfxRenderer& renderer, const light_panel::RowLayout& row, const bool picked) const {
    if (!picked) return;
    renderer.fillRect(light_panel::kSidePad, row.y + row.height + topInset_ - 1,
                      layout_.width - light_panel::kSidePad * 2, 1, false);
  }

  bool active_ = false;
  bool on_ = false;
  uint8_t brightness_ = 0;
  uint8_t warmth_ = 0;
  light_panel::Row selected_ = light_panel::Row::Brightness;
  mutable int topInset_ = 0;
  mutable light_panel::Layout layout_{};
  ButtonNavigator nav_{120, 350};
};
