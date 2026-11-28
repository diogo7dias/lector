#pragma once

#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>

#include "BannerStyle.h"
#include "CrossPointSettings.h"
#include "LightPanelContext.h"
#include "LightPanelDraw.h"
#include "LightPanelGeometry.h"
#include "MappedInputManager.h"
#include "SliderLook.h"
#include "fontIds.h"
#include "icons/sun24.h"
#include "icons/thermometer24.h"
#include "util/BoundMenuLabels.h"
#include "util/ButtonNavigator.h"

// The light panel: a band pulled down from the top edge holding the frontlight's controls
// and the actions that make sense where you are. Drawn over the page without clearing it,
// like OptionPopup, so the page is still there to judge the light against. White with
// black text, like the message banner; the page below is stippled one pixel in two.
//
// Every light change applies to the hardware immediately; SETTINGS is written once, on
// close, so a drag across the track does not spend a file write per pixel.
class LightPanel {
 public:
  // buildContext fills in what belongs in the panel here; runAction is handed one of the
  // CrossPointSettings::LP_MENU_* values and returns whether anything took it; stepAux
  // moves the aux row by -1 or +1 and returns whether the value changed.
  void setHost(std::function<void(light_panel::Context&)> buildContext, std::function<bool(uint8_t)> runAction,
               std::function<bool(int)> stepAux) {
    buildContext_ = std::move(buildContext);
    runAction_ = std::move(runAction);
    stepAux_ = std::move(stepAux);
  }

  // Opens on every board. Without a frontlight (X3, X4) it is the aux row, the action grid
  // and the readout, driven by keys alone.
  void show() {
    on_ = Frontlight.isOn();
    brightness_ = Frontlight.brightness();
    warmth_ = Frontlight.warmth();
    actionPressed_ = -1;
    dragging_ = light_panel::Row::None;
    sliderThrottle_.reset();
    rebuildContext();
    // The first row the layout will show; render() has not run yet, so it is worked out here.
    if (Frontlight.present()) {
      cursor_ = {light_panel::Row::Brightness, -1};
    } else if (context_.hasAux()) {
      cursor_ = {light_panel::Row::Aux, -1};
    } else {
      cursor_ = {light_panel::Row::None, context_.actionCount > 0 ? 0 : -1};
    }
    readFreeSpace();
    active_ = true;
    LOG_DBG("PANEL", "Open (%d actions)", context_.actionCount);
  }

  bool isActive() const { return active_; }

  void close(const std::function<void()>& requestUpdate) {
    active_ = false;
    sliderThrottle_.reset();
    LOG_DBG("PANEL", "Close");
    const uint8_t on = on_ ? 1 : 0;
    // A panel opened and closed unchanged costs no settings write (write throttling). With no
    // frontlight the values were never the user's to change, so nothing is written either.
    if (Frontlight.present() && (SETTINGS.frontlightOn != on || SETTINGS.frontlightBrightness != brightness_ ||
                                 SETTINGS.frontlightWarmth != warmth_)) {
      SETTINGS.frontlightOn = on;
      SETTINGS.frontlightBrightness = brightness_;
      SETTINGS.frontlightWarmth = warmth_;
      SETTINGS.saveToFile();
    }
    requestUpdate();
  }

  bool handleInput(MappedInputManager& input, const std::function<void()>& requestUpdate) {
    if (!active_) return false;

    // Confirm on a grid button fires it: the keys-only boards have no other way to.
    if (cursor_.onGrid() && cursor_.action < context_.actionCount &&
        input.wasPressed(MappedInputManager::Button::Confirm)) {
      runAction(context_.actions[cursor_.action], requestUpdate);
      return true;
    }

    // A second top swipe puts the panel away: the gesture that opened it is the one the
    // hand is already making.
    if (input.wasMenuGesture() || input.wasPressed(MappedInputManager::Button::Back) ||
        input.wasPressed(MappedInputManager::Button::Confirm)) {
      close(requestUpdate);
      return true;
    }

    int tx = 0;
    int ty = 0;
    const bool down = input.wasScreenTouchDown(tx, ty);
    const bool held = !down && input.isScreenTouchHeld(tx, ty);

    if (!down && !held) {
      // An action fires when the finger comes up inside the button it went down in. Every
      // one of them leaves this screen behind, so none may go off under a thumb that was
      // dragging a track and drifted low.
      const int fired = input.wasScreenTouchReleased() ? actionPressed_ : -1;
      // Cleared either way. A finger that leaves the digitiser without a release event
      // would otherwise leave the press armed, and the next unrelated release would fire
      // a button nobody was touching.
      actionPressed_ = -1;
      const bool wasDragging = dragging_ != light_panel::Row::None;
      dragging_ = light_panel::Row::None;
      if (wasDragging && sliderThrottle_.flushOnRelease()) {
        requestUpdate();
        return true;
      }
      if (fired >= 0 && fired < context_.actionCount) {
        runAction(context_.actions[fired], requestUpdate);
        return true;
      }
    }

    if (down || held) {
      const int localY = ty - topInset_;
      // A drag that began on a track owns every following frame, wherever the finger has
      // wandered to: letting the hit test speak again mid-drag would hand the finger to
      // whatever it slid over. `down` repeats the landing point while the contact is a tap
      // candidate, so follow the live position or a small nudge at 0% or 100% does nothing.
      if (dragging_ != light_panel::Row::None) {
        int lx = tx;
        int ly = ty;
        input.isScreenTouchHeld(lx, ly);
        const auto& bar = dragging_ == light_panel::Row::Warmth ? layout_.warmth.bar : layout_.brightness.bar;
        const uint32_t now = millis();
        if (setValue(dragging_, light_panel::valueForX(bar, lx, 0, maxFor(dragging_)))) {
          if (sliderThrottle_.onDragValueChanged(now)) requestUpdate();
        } else if (sliderThrottle_.shouldFlush(now)) {
          requestUpdate();
        }
        return true;
      }

      const auto hit = light_panel::hitTest(layout_, tx, localY);
      if (actionPressed_ >= 0) {
        // Still down: keep the press only while the finger stays on it.
        if (hit.kind != light_panel::Hit::Kind::Action || hit.action != actionPressed_) actionPressed_ = -1;
        return true;
      }

      if (!light_panel::insidePanel(layout_, tx, localY)) {
        // A tap on the page below is "put it away", not a page turn: the host returns
        // early on a consumed input, so the tap cannot also reach the reader.
        if (down) {
          input.spendTouchContact();
          close(requestUpdate);
        }
        return true;
      }
      if (!down) return true;  // a hold that started on nothing stays on nothing

      switch (hit.kind) {
        // A step or a toggle acts once per press: wasScreenTouchDown stays true for as long
        // as the finger rests, so the contact is spent here or a held finger repeats it.
        case light_panel::Hit::Kind::Toggle:
          input.spendTouchContact();
          on_ = !on_;
          applyLight();
          requestUpdate();
          break;
        case light_panel::Hit::Kind::Step:
          input.spendTouchContact();
          cursor_ = {hit.row, -1};
          step(hit.row, hit.delta, requestUpdate);
          break;
        case light_panel::Hit::Kind::Track: {
          cursor_ = {hit.row, -1};
          dragging_ = hit.row;
          sliderThrottle_.reset();
          const auto& bar = hit.row == light_panel::Row::Warmth ? layout_.warmth.bar : layout_.brightness.bar;
          if (setValue(hit.row, light_panel::valueForX(bar, tx, 0, maxFor(hit.row))) &&
              sliderThrottle_.onDragValueChanged(millis())) {
            requestUpdate();
          }
          break;
        }
        case light_panel::Hit::Kind::Action:
          // Armed silently: a pressed-look frame cost a refresh (~400 ms) that the action
          // then waited behind. The action leaves the panel anyway, straight back to the page.
          actionPressed_ = hit.action;
          break;
        default:
          break;
      }
      return true;
    }

    // Buttons: Up/Down move one cursor over the value rows and on through the action grid,
    // Left/Right step a row (the same small step the numeric settings rows use) or move
    // along the grid, and Confirm fires the button under the cursor (above).
    nav_.onPressAndContinuous({MappedInputManager::Button::Up}, [this, &requestUpdate] {
      moveCursor(light_panel::moveVertical(layout_, cursor_, -1), requestUpdate);
    });
    nav_.onPressAndContinuous({MappedInputManager::Button::Down}, [this, &requestUpdate] {
      moveCursor(light_panel::moveVertical(layout_, cursor_, 1), requestUpdate);
    });
    // One step per press, held or not, like the steppers on screen.
    nav_.onPress({MappedInputManager::Button::Left}, [this, &requestUpdate] { stepOrMove(-1, requestUpdate); });
    nav_.onPress({MappedInputManager::Button::Right}, [this, &requestUpdate] { stepOrMove(1, requestUpdate); });
    return true;
  }

  // Records the layout it drew, so handleInput hit-tests exactly what is on screen.
  void render(const GfxRenderer& renderer) const {  // NOLINT: caches layout for touch
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
      drawBox(renderer, layout_.actions[i], I18N.get(boundMenuActionLabel(context_.actions[i])), cursor_.action == i);
    }
    light_panel::drawReadout(renderer, layout_.readout, topInset_, freeBytes_);
  }

  bool processRender(GfxRenderer& renderer) const {
    if (!active_) return false;
    render(renderer);
    renderer.displayBuffer();
    return true;
  }

 private:
  void rebuildContext() {
    context_ = light_panel::Context{};
    if (buildContext_) buildContext_(context_);
  }

  // Read once per opening, not per frame: sdUsedBytes walks the allocation table, which is
  // far too much work to repeat on every drag of the brightness track.
  void readFreeSpace() {
    const uint64_t total = Storage.sdTotalBytes();
    const uint64_t used = Storage.sdUsedBytes();
    freeBytes_ = total > used ? total - used : 0;
  }

  void runAction(const uint8_t function, const std::function<void()>& requestUpdate) {
    // Every action leaves this screen behind, so the panel's own values are written first
    // and the panel is shut before the host acts. The page is repainted once, after the
    // action: repainting on close painted the old page with a full cleanup refresh (about
    // 1.7 s) only for a relayout to replace it straight away.
    close([] {});
    if (runAction_) runAction_(function);
    requestUpdate();
  }

  // Frontlight as a menu row: the name at the left, On or Off at the right.
  void drawToggleRow(const GfxRenderer& renderer) const {
    const auto& rect = layout_.toggle;
    const int textY = rect.y + topInset_ + (rect.height - renderer.getLineHeight(banner::FONT_ID)) / 2;
    renderer.drawText(banner::FONT_ID, rect.x, textY, I18N.get(StrId::STR_FRONTLIGHT), true);
    const char* state = I18N.get(on_ ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
    renderer.drawText(banner::FONT_ID, rect.x + rect.width - renderer.getTextWidth(banner::FONT_ID, state), textY,
                      state, true);
  }

  // A framed box with its label centred; filled black with white text under the key cursor.
  void drawBox(const GfxRenderer& renderer, const light_panel::Rect& rect, const char* label,
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
  void drawSliderRow(const GfxRenderer& renderer, const light_panel::StepRow& row, const freeink::Icon& icon,
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
  void drawAuxRow(const GfxRenderer& renderer, const light_panel::StepRow& row, const char* label) const {
    if (row.height == 0) return;
    const bool ink = !drawSelection(renderer, row);
    drawSteppers(renderer, row, ink);
    drawCentered(renderer, row.value, label, /*center=*/true, ink);
  }

  void drawSteppers(const GfxRenderer& renderer, const light_panel::StepRow& row, const bool ink) const {
    slider_look::drawStepper(renderer, row.minus.x, row.minus.y + topInset_, row.minus.width, row.minus.height,
                             /*plus=*/false, ink);
    slider_look::drawStepper(renderer, row.plus.x, row.plus.y + topInset_, row.plus.width, row.plus.height,
                             /*plus=*/true, ink);
  }

  // Vertically centred in `rect` either way; `center` picks horizontal centring over
  // starting at the left edge.
  void drawCentered(const GfxRenderer& renderer, const light_panel::Rect& rect, const char* text, const bool center,
                    const bool ink) const {
    const int textY = rect.y + topInset_ + (rect.height - renderer.getLineHeight(banner::FONT_ID)) / 2;
    const int textX = center ? rect.x + (rect.width - renderer.getTextWidth(banner::FONT_ID, text)) / 2 : rect.x;
    renderer.drawText(banner::FONT_ID, textX, textY, text, ink);
  }

  // Button users need to see which row Left/Right will move: it fills black edge to edge,
  // like the in-book menu's selected row, and the row is then drawn in white. True when
  // the row is selected.
  bool drawSelection(const GfxRenderer& renderer, const light_panel::StepRow& row) const {
    if (cursor_.onGrid() || cursor_.row != row.row) return false;
    renderer.fillRect(0, row.y + topInset_, layout_.width, row.height, true);
    return true;
  }

  void applyLight() {
    Frontlight.setOn(on_);
    if (on_) {
      Frontlight.setBrightness(brightness_);
      Frontlight.setWarmth(warmth_);
    }
  }

  // The top of a row's range: brightness stops at Settings > Max Brightness, warmth runs
  // the whole 0-100.
  static int maxFor(const light_panel::Row row) {
    return row == light_panel::Row::Brightness ? std::max<int>(1, SETTINGS.frontlightMaxBrightness) : 100;
  }

  // Moving either track off zero turns the light back on: the user dragged brightness up,
  // so asking them to also find the toggle would be a puzzle, not a control.
  bool setValue(const light_panel::Row row, const int raw) {
    if (row != light_panel::Row::Brightness && row != light_panel::Row::Warmth) return false;
    const auto next = static_cast<uint8_t>(std::clamp(raw, 0, maxFor(row)));
    uint8_t& target = row == light_panel::Row::Warmth ? warmth_ : brightness_;
    if (target == next) return false;
    target = next;
    if (row == light_panel::Row::Brightness && next > 0) on_ = true;
    applyLight();
    return true;
  }

  void step(const light_panel::Row row, const int delta, const std::function<void()>& requestUpdate) {
    if (row == light_panel::Row::Aux) {
      if (!stepAux_ || !stepAux_(delta)) return;
      // The row shows the value it just moved, so the text has to be asked for again.
      rebuildContext();
      requestUpdate();
      return;
    }
    const uint8_t current = row == light_panel::Row::Warmth ? warmth_ : brightness_;
    if (setValue(row, current + delta)) requestUpdate();
  }

  void moveCursor(const light_panel::Cursor next, const std::function<void()>& requestUpdate) {
    if (next == cursor_) return;
    cursor_ = next;
    requestUpdate();
  }

  void stepOrMove(const int delta, const std::function<void()>& requestUpdate) {
    if (cursor_.onGrid()) {
      moveCursor(light_panel::moveHorizontal(layout_, cursor_, delta), requestUpdate);
    } else {
      step(cursor_.row, delta, requestUpdate);
    }
  }

  bool active_ = false;
  bool on_ = false;
  uint8_t brightness_ = 0;
  uint8_t warmth_ = 0;
  uint64_t freeBytes_ = 0;
  light_panel::Cursor cursor_{light_panel::Row::Brightness, -1};
  light_panel::Row dragging_ = light_panel::Row::None;
  light_panel::SliderThrottle sliderThrottle_{};
  int actionPressed_ = -1;
  light_panel::Context context_{};
  std::function<void(light_panel::Context&)> buildContext_;
  std::function<bool(uint8_t)> runAction_;
  std::function<bool(int)> stepAux_;
  mutable int topInset_ = 0;
  mutable light_panel::Layout layout_{};
  ButtonNavigator nav_{120, 350};
};
