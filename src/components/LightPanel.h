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
#include "util/OrientationCycle.h"

// The light panel: a band pulled down from the top edge holding the frontlight's three
// controls and two actions. Drawn over whatever is on screen without clearing it, the
// same way OptionPopup and ValueBarPopup are, so the page you were reading is still there
// to judge the light against — which is the whole reason to reach for it mid-page.
//
// White with black text, unlike the banner style it borrows its metrics from. It gained
// two text buttons, and a filled button inside a black band inverts twice; a mostly-white
// band is also the cheaper of the two to paint on e-ink.
//
// The page below is stippled rather than left plain: one pixel in four turned black,
// which reads as a shade drawn over the page without erasing a word of it. Drawn once,
// when the panel opens.
//
// Every change applies to the hardware immediately; SETTINGS is written once, on close,
// so a drag across the bar does not spend a file write per pixel.
class LightPanel {
 public:
  // onSleep: only main.cpp can put the device down, so the panel names the action and the
  // host runs it. onRotate is handed the orientation to move to and does NOT write it:
  // the reader's own applyOrientation persists it and re-indexes the chapter at the new
  // column width, and it no-ops when the setting already holds the value.
  void setActions(std::function<void()> onSleep, std::function<void(uint8_t)> onRotate) {
    onSleep_ = std::move(onSleep);
    onRotate_ = std::move(onRotate);
  }

  void show() {
    if (!Frontlight.present()) return;
    on_ = Frontlight.isOn();
    brightness_ = Frontlight.brightness();
    warmth_ = Frontlight.warmth();
    selected_ = light_panel::Row::Brightness;
    pressed_ = light_panel::Button::None;
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

    // A button fires when the finger comes up inside the button it went down in. Sleep
    // puts the device away and Rotate rebuilds the page, so neither may go off under a
    // thumb that was dragging the warmth bar and drifted low.
    if (!down && !held) {
      const auto fired = input.wasScreenTouchReleased() ? pressed_ : light_panel::Button::None;
      // Cleared either way. A finger that leaves the digitiser without a release event
      // would otherwise leave the press armed, and the next unrelated release would fire
      // a button nobody was touching.
      const bool wasArmed = pressed_ != light_panel::Button::None;
      pressed_ = light_panel::Button::None;
      if (fired != light_panel::Button::None) {
        runButton(fired, requestUpdate);
        return true;
      }
      if (wasArmed) requestUpdate();
    }

    if (down || held) {
      const int localY = ty - topInset_;
      const auto button = light_panel::buttonAt(layout_, tx, localY);
      if (down && button != light_panel::Button::None) {
        pressed_ = button;
        requestUpdate();
        return true;
      }
      if (pressed_ != light_panel::Button::None) {
        // Still down: keep the press only while the finger stays on it.
        if (button != pressed_) {
          pressed_ = light_panel::Button::None;
          requestUpdate();
        }
        return true;
      }
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
    stipplePageBelow(renderer, bandHeight);

    renderer.fillRect(0, 0, screenWidth, bandHeight, false);
    renderer.fillRect(0, bandHeight - banner::RULE, screenWidth, banner::RULE, true);

    drawToggleRow(renderer);
    drawBarRow(renderer, layout_.brightness, I18N.get(StrId::STR_FRONTLIGHT_BRIGHTNESS), brightness_);
    if (layout_.hasWarmth) {
      drawBarRow(renderer, layout_.warmth, I18N.get(StrId::STR_FRONTLIGHT_WARMTH), warmth_);
    }
    drawButton(renderer, layout_.sleep, I18N.get(StrId::STR_SLEEP), pressed_ == light_panel::Button::Sleep);
    drawButton(renderer, layout_.rotate, I18N.get(StrId::STR_ROTATE), pressed_ == light_panel::Button::Rotate);
  }

  bool processRender(GfxRenderer& renderer) const {
    if (!active_) return false;
    render(renderer);
    renderer.displayBuffer();
    return true;
  }

 private:
  void runButton(const light_panel::Button button, const std::function<void()>& requestUpdate) {
    // Both actions leave this screen behind, so the panel's own values are written first
    // and the panel is shut before the host acts.
    close(requestUpdate);
    if (button == light_panel::Button::Sleep) {
      if (onSleep_) onSleep_();
      return;
    }
    if (onRotate_) onRotate_(orientation_cycle::next(SETTINGS.orientation));
  }

  // One pixel in four, on a 4x4 grid: dark enough to read as a shade over the page and
  // sparse enough to leave the words under it legible. Only black pixels are written, so
  // the page is darkened rather than replaced — which is why this is a loop and not
  // fillRectDither(LightGray), whose white pixels would erase it.
  void stipplePageBelow(const GfxRenderer& renderer, const int bandHeight) const {
    const int width = renderer.getScreenWidth();
    const int height = renderer.getScreenHeight();
    for (int y = bandHeight; y < height; y += 2) {
      for (int x = 0; x < width; x += 2) renderer.drawPixel(x, y, true);
    }
  }

  void drawButton(const GfxRenderer& renderer, const light_panel::Rect& rect, const char* label,
                  const bool pressed) const {
    const int y = rect.y + topInset_;
    if (pressed) renderer.fillRect(rect.x, y, rect.width, rect.height, true);
    renderer.drawRect(rect.x, y, rect.width, rect.height, true);
    const int textWidth = renderer.getTextWidth(banner::FONT_ID, label);
    const int textX = rect.x + (rect.width - textWidth) / 2;
    const int textY = y + light_panel::kButtonPadY;
    renderer.drawText(banner::FONT_ID, textX, textY, label, pressed);
  }

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
    renderer.drawText(banner::FONT_ID, light_panel::kSidePad, y, I18N.get(StrId::STR_FRONTLIGHT), true);
    const char* state = I18N.get(on_ ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
    renderer.drawText(banner::FONT_ID, light_panel::kSidePad + light_panel::kLabelWidth, y, state, true);
  }

  void drawBarRow(const GfxRenderer& renderer, const light_panel::RowLayout& row, const char* label,
                  const uint8_t value) const {
    const int y = row.y + topInset_;
    drawSelection(renderer, row, selected_ == row.row);
    char text[48];
    snprintf(text, sizeof(text), "%s %d", label, static_cast<int>(value));
    renderer.drawText(banner::FONT_ID, light_panel::kSidePad, y, text, true);

    const auto& bar = row.bar;
    const int barY = bar.y + topInset_;
    renderer.drawRect(bar.x, barY, bar.width, bar.height, true);
    const int fill = bar.width * value / 100;
    if (fill > 0) renderer.fillRect(bar.x, barY, fill, bar.height, true);
  }

  // Button users need to see which row Left/Right will move. A rule under the row rather
  // than a box: a full frame fought with the bar.
  void drawSelection(const GfxRenderer& renderer, const light_panel::RowLayout& row, const bool picked) const {
    if (!picked) return;
    renderer.fillRect(light_panel::kSidePad, row.y + row.height + topInset_ - 1,
                      layout_.width - light_panel::kSidePad * 2, 1, true);
  }

  bool active_ = false;
  bool on_ = false;
  uint8_t brightness_ = 0;
  uint8_t warmth_ = 0;
  light_panel::Row selected_ = light_panel::Row::Brightness;
  light_panel::Button pressed_ = light_panel::Button::None;
  std::function<void()> onSleep_;
  std::function<void(uint8_t)> onRotate_;
  mutable int topInset_ = 0;
  mutable light_panel::Layout layout_{};
  ButtonNavigator nav_{120, 350};
};
