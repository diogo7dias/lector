#pragma once

#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>

#include "BannerStyle.h"
#include "CrossPointSettings.h"
#include "LightPanelGeometry.h"
#include "MappedInputManager.h"
#include "fontIds.h"
#include "icons/sun24.h"
#include "icons/thermometer24.h"
#include "util/BoundMenuLabels.h"
#include "util/ButtonNavigator.h"
#include "util/DebugTrace.h"

// The light panel: a band pulled down from the top edge holding the frontlight's controls
// and the actions that make sense where you are. Drawn over whatever is on screen without
// clearing it, the same way OptionPopup and SliderBand are, so the page you were
// reading is still there to judge the light against — which is the whole reason to reach
// for it mid-page.
//
// White with black text, unlike the banner style it borrows its metrics from: it is mostly
// buttons, and a filled button inside a black band inverts twice. A mostly-white band is
// also the cheaper of the two to paint on e-ink.
//
// The page below is stippled, one pixel in two, which reads as a shade drawn over the page
// without erasing a word of it.
//
// Every light change applies to the hardware immediately; SETTINGS is written once, on
// close, so a drag across the track does not spend a file write per pixel.
namespace light_panel {

// What the host puts in the panel for the screen that is currently up. Rebuilt every time
// the panel opens and after every step, so a value shown in the aux row is never stale.
struct Context {
  // The aux row: Text Size in a book, Sort outside one. Empty text means the context has
  // nothing to step, and the row is not drawn at all.
  char auxText[48] = {};
  // CrossPointSettings::LONG_PRESS_MENU_FUNCTION values, in grid order.
  uint8_t actions[kMaxActions] = {};
  int actionCount = 0;

  bool hasAux() const { return auxText[0] != '\0'; }
};

}  // namespace light_panel

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

  void show() {
    debug_trace::note("light panel show(), present=%d", Frontlight.present() ? 1 : 0);
    if (!Frontlight.present()) return;
    on_ = Frontlight.isOn();
    brightness_ = Frontlight.brightness();
    warmth_ = Frontlight.warmth();
    selected_ = light_panel::Row::Brightness;
    actionPressed_ = -1;
    dragging_ = light_panel::Row::None;
    rebuildContext();
    readFreeSpace();
    active_ = true;
  }

  bool isActive() const { return active_; }

  bool handleInput(MappedInputManager& input, const std::function<void()>& requestUpdate) {
    if (!active_) return false;

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
      const bool wasArmed = actionPressed_ >= 0;
      actionPressed_ = -1;
      dragging_ = light_panel::Row::None;
      if (fired >= 0 && fired < context_.actionCount) {
        runAction(context_.actions[fired], requestUpdate);
        return true;
      }
      if (wasArmed) requestUpdate();
    }

    if (down || held) {
      const int localY = ty - topInset_;
      // A drag that began on a track owns every following frame, wherever the finger has
      // wandered to: letting the hit test speak again mid-drag would hand the finger to
      // whatever it slid over.
      if (!down && dragging_ != light_panel::Row::None) {
        const auto& bar = dragging_ == light_panel::Row::Warmth ? layout_.warmth.bar : layout_.brightness.bar;
        if (setValue(dragging_, light_panel::valueForX(bar, tx, 0, 100))) requestUpdate();
        return true;
      }

      const auto hit = light_panel::hitTest(layout_, tx, localY);
      if (actionPressed_ >= 0) {
        // Still down: keep the press only while the finger stays on it.
        if (hit.kind != light_panel::Hit::Kind::Action || hit.action != actionPressed_) {
          actionPressed_ = -1;
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
      if (!down) return true;  // a hold that started on nothing stays on nothing

      switch (hit.kind) {
        case light_panel::Hit::Kind::Toggle:
          on_ = !on_;
          applyLight();
          requestUpdate();
          break;
        case light_panel::Hit::Kind::Step:
          selected_ = hit.row;
          step(hit.row, hit.delta, requestUpdate);
          break;
        case light_panel::Hit::Kind::Track: {
          selected_ = hit.row;
          dragging_ = hit.row;
          const auto& bar = hit.row == light_panel::Row::Warmth ? layout_.warmth.bar : layout_.brightness.bar;
          if (setValue(hit.row, light_panel::valueForX(bar, tx, 0, 100))) requestUpdate();
          break;
        }
        case light_panel::Hit::Kind::Action:
          actionPressed_ = hit.action;
          requestUpdate();
          break;
        default:
          break;
      }
      return true;
    }

    // Buttons: Up/Down pick the row, Left/Right step it. Same small step the numeric
    // settings rows use, so the two feel like one control. The action grid is touch-only:
    // on a board without touch the panel cannot be opened by a gesture in the first place.
    nav_.onPressAndContinuous({MappedInputManager::Button::Up},
                              [this, &requestUpdate] { moveSelection(-1, requestUpdate); });
    nav_.onPressAndContinuous({MappedInputManager::Button::Down},
                              [this, &requestUpdate] { moveSelection(1, requestUpdate); });
    nav_.onPressAndContinuous({MappedInputManager::Button::Left},
                              [this, &requestUpdate] { step(selected_, -1, requestUpdate); });
    nav_.onPressAndContinuous({MappedInputManager::Button::Right},
                              [this, &requestUpdate] { step(selected_, 1, requestUpdate); });
    return true;
  }

  // Records the layout it drew, so handleInput hit-tests exactly what is on screen.
  void render(const GfxRenderer& renderer) const {  // NOLINT: caches layout for touch
    if (!active_) return;

    const int screenWidth = renderer.getScreenWidth();
    const int lineHeight = renderer.getLineHeight(banner::FONT_ID);
    layout_ = light_panel::forScreen(screenWidth, lineHeight, renderer.getLineHeight(SMALL_FONT_ID),
                                     Frontlight.hasColorTemperature(), context_.hasAux(), context_.actionCount);

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

    char text[64];
    snprintf(text, sizeof(text), "%s  %s", I18N.get(StrId::STR_FRONTLIGHT),
             I18N.get(on_ ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF));
    drawBox(renderer, layout_.toggle, text, /*filled=*/on_);

    drawSliderRow(renderer, layout_.brightness, Sun24Icon, brightness_);
    if (layout_.hasWarmth) drawSliderRow(renderer, layout_.warmth, Thermometer24Icon, warmth_);
    if (layout_.hasAux) drawAuxRow(renderer, layout_.aux, context_.auxText);

    for (int i = 0; i < layout_.actionCount; ++i) {
      drawBox(renderer, layout_.actions[i], I18N.get(boundMenuActionLabel(context_.actions[i])),
              /*filled=*/i == actionPressed_);
    }
    drawReadout(renderer);
  }

  bool processRender(GfxRenderer& renderer) const {
    if (!active_) return false;
    debug_trace::note("light panel rendering");
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
    // and the panel is shut before the host acts.
    close(requestUpdate);
    if (runAction_) runAction_(function);
  }

  // One pixel in two, on a checkerboard: dark enough to read as a shade over the page and
  // sparse enough to leave the words under it legible. Only black pixels are written, so
  // the page is darkened rather than replaced — which is why this is a loop and not
  // fillRectDither(LightGray), whose white pixels would erase it.
  void stipplePageBelow(const GfxRenderer& renderer, const int bandHeight) const {
    const int width = renderer.getScreenWidth();
    const int height = renderer.getScreenHeight();
    for (int y = bandHeight; y < height; ++y) {
      for (int x = (y & 1); x < width; x += 2) renderer.drawPixel(x, y, true);
    }
  }

  // A framed box with its label centred. Filled while pressed, and the text is knocked out
  // of the fill rather than drawn over it.
  void drawBox(const GfxRenderer& renderer, const light_panel::Rect& rect, const char* label, const bool filled) const {
    const int y = rect.y + topInset_;
    if (filled) renderer.fillRect(rect.x, y, rect.width, rect.height, true);
    renderer.drawRect(rect.x, y, rect.width, rect.height, true);
    const int textWidth = renderer.getTextWidth(banner::FONT_ID, label);
    const int textY = y + (rect.height - renderer.getLineHeight(banner::FONT_ID)) / 2;
    renderer.drawText(banner::FONT_ID, rect.x + (rect.width - textWidth) / 2, textY, label, !filled);
  }

  // Icon, track, number, then the two steppers. The row is named by its icon rather than
  // by a word: the word and the track were fighting for the same 44 px, and the icon says
  // which value this is in a third of the space.
  void drawSliderRow(const GfxRenderer& renderer, const light_panel::StepRow& row, const freeink::Icon& icon,
                     const uint8_t value) const {
    if (row.height == 0) return;
    drawSteppers(renderer, row);

    const int rowCenter = row.y + topInset_ + row.height / 2;
    renderer.drawIcon(icon.bits, row.icon.x, rowCenter - icon.opticalCenterY, icon.w);

    const int barY = row.bar.y + topInset_;
    renderer.drawRect(row.bar.x, barY, row.bar.width, row.bar.height, true);
    const int fill = row.bar.width * value / 100;
    if (fill > 0) renderer.fillRect(row.bar.x, barY, fill, row.bar.height, true);

    char number[8];
    snprintf(number, sizeof(number), "%d", static_cast<int>(value));
    drawCentered(renderer, row.value, number, /*center=*/false);
    drawSelection(renderer, row);
  }

  // No icon and no track: the label owns the run up to the stepper column.
  void drawAuxRow(const GfxRenderer& renderer, const light_panel::StepRow& row, const char* label) const {
    if (row.height == 0) return;
    drawSteppers(renderer, row);
    drawCentered(renderer, row.value, label, /*center=*/true);
    drawSelection(renderer, row);
  }

  void drawSteppers(const GfxRenderer& renderer, const light_panel::StepRow& row) const {
    drawBox(renderer, row.minus, "-", /*filled=*/false);
    drawBox(renderer, row.plus, "+", /*filled=*/false);
  }

  // Vertically centred in `rect` either way; `center` picks horizontal centring over
  // starting at the left edge.
  void drawCentered(const GfxRenderer& renderer, const light_panel::Rect& rect, const char* text,
                    const bool center) const {
    const int textY = rect.y + topInset_ + (rect.height - renderer.getLineHeight(banner::FONT_ID)) / 2;
    const int textX = center ? rect.x + (rect.width - renderer.getTextWidth(banner::FONT_ID, text)) / 2 : rect.x;
    renderer.drawText(banner::FONT_ID, textX, textY, text, true);
  }

  // Button users need to see which row Left/Right will move. A rule under the row rather
  // than a box: a full frame fought with the steppers.
  void drawSelection(const GfxRenderer& renderer, const light_panel::StepRow& row) const {
    if (selected_ != row.row) return;
    renderer.fillRect(light_panel::kSidePad, row.y + row.height + topInset_ - 1,
                      layout_.width - light_panel::kSidePad * 2, 1, true);
  }

  // Battery and free space, in the small UI font. No rule above it: the button grid it
  // follows is already a row of boxes, and a line under those read as a second border.
  void drawReadout(const GfxRenderer& renderer) const {
    const auto& rect = layout_.readout;
    const int y = rect.y + topInset_;

    char left[32];
    snprintf(left, sizeof(left), "%s %u%%", I18N.get(StrId::STR_BATTERY),
             static_cast<unsigned>(powerManager.getBatteryPercentage()));
    renderer.drawText(SMALL_FONT_ID, rect.x, y, left, true);

    char right[32];
    // Whole gigabytes below ten get a decimal; above it the tenth is noise on a card this
    // size and the shorter string is easier to read at a glance.
    const double gigabytes = static_cast<double>(freeBytes_) / (1024.0 * 1024.0 * 1024.0);
    if (gigabytes < 10.0) {
      snprintf(right, sizeof(right), "%.1f GB %s", gigabytes, I18N.get(StrId::STR_FREE_SPACE));
    } else {
      snprintf(right, sizeof(right), "%d GB %s", static_cast<int>(gigabytes + 0.5), I18N.get(StrId::STR_FREE_SPACE));
    }
    const int width = renderer.getTextWidth(SMALL_FONT_ID, right);
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - width, y, right, true);
  }

  void close(const std::function<void()>& requestUpdate) {
    active_ = false;
    const uint8_t on = on_ ? 1 : 0;
    // A panel opened and closed unchanged costs no settings write (write throttling).
    if (SETTINGS.frontlightOn != on || SETTINGS.frontlightBrightness != brightness_ ||
        SETTINGS.frontlightWarmth != warmth_) {
      SETTINGS.frontlightOn = on;
      SETTINGS.frontlightBrightness = brightness_;
      SETTINGS.frontlightWarmth = warmth_;
      SETTINGS.saveToFile();
    }
    requestUpdate();
  }

  void applyLight() {
    Frontlight.setOn(on_);
    if (on_) {
      Frontlight.setBrightness(brightness_);
      Frontlight.setWarmth(warmth_);
    }
  }

  // Moving either track off zero turns the light back on: the user dragged brightness up,
  // so asking them to also find the toggle would be a puzzle, not a control.
  bool setValue(const light_panel::Row row, const int raw) {
    if (row != light_panel::Row::Brightness && row != light_panel::Row::Warmth) return false;
    const auto next = static_cast<uint8_t>(std::clamp(raw, 0, 100));
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

  void moveSelection(const int delta, const std::function<void()>& requestUpdate) {
    light_panel::Row order[3] = {light_panel::Row::Brightness, light_panel::Row::Warmth, light_panel::Row::Aux};
    int count = 1;
    if (Frontlight.hasColorTemperature()) {
      count = 2;
    } else {
      order[1] = light_panel::Row::Aux;
    }
    if (context_.hasAux()) count++;
    int index = 0;
    for (int i = 0; i < count; ++i) {
      if (order[i] == selected_) index = i;
    }
    selected_ = order[std::clamp(index + delta, 0, count - 1)];
    requestUpdate();
  }

  bool active_ = false;
  bool on_ = false;
  uint8_t brightness_ = 0;
  uint8_t warmth_ = 0;
  uint64_t freeBytes_ = 0;
  light_panel::Row selected_ = light_panel::Row::Brightness;
  light_panel::Row dragging_ = light_panel::Row::None;
  int actionPressed_ = -1;
  light_panel::Context context_{};
  std::function<void(light_panel::Context&)> buildContext_;
  std::function<bool(uint8_t)> runAction_;
  std::function<bool(int)> stepAux_;
  mutable int topInset_ = 0;
  mutable light_panel::Layout layout_{};
  ButtonNavigator nav_{120, 350};
};
