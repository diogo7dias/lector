#include "UiStatusActivity.h"

// UiStatusActivity's centred and section body shapes, the slider band, the number pad and
// the QR, signal and progress marks. Split from UiStatusActivity.cpp for the token limit.

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/ComparisonLayout.h"
#include "components/LightPanelGeometry.h"
#include "components/SignalMeter.h"
#include "components/SliderField.h"
#include "components/SliderLook.h"
#include "components/StatusStack.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "util/QrUtils.h"

namespace fui = freeink::ui;

// The square every QR on these screens is drawn at: big enough for a phone to
// read across a desk, small enough that two fit on a portrait panel.
constexpr int16_t kQrSize = 198;

// The number pad: a phone's digit block, with delete and OK on the bottom row. Digits
// carry their character as the key value, so delete's 8 and OK's 13 cannot be mistaken
// for one.
constexpr fui::KeyboardKey kKeypadRow1[] = {{"1", nullptr, fui::KeyKind::Normal, fui::StateNormal, '1'},
                                            {"2", nullptr, fui::KeyKind::Normal, fui::StateNormal, '2'},
                                            {"3", nullptr, fui::KeyKind::Normal, fui::StateNormal, '3'}};
constexpr fui::KeyboardKey kKeypadRow2[] = {{"4", nullptr, fui::KeyKind::Normal, fui::StateNormal, '4'},
                                            {"5", nullptr, fui::KeyKind::Normal, fui::StateNormal, '5'},
                                            {"6", nullptr, fui::KeyKind::Normal, fui::StateNormal, '6'}};
constexpr fui::KeyboardKey kKeypadRow3[] = {{"7", nullptr, fui::KeyKind::Normal, fui::StateNormal, '7'},
                                            {"8", nullptr, fui::KeyKind::Normal, fui::StateNormal, '8'},
                                            {"9", nullptr, fui::KeyKind::Normal, fui::StateNormal, '9'}};
constexpr fui::KeyboardKey kKeypadRow4[] = {
    {"", nullptr, fui::KeyKind::Delete, fui::StateNormal, fui::QWERTY_KEY_BACKSPACE},
    {"0", nullptr, fui::KeyKind::Normal, fui::StateNormal, '0'},
    {"OK", nullptr, fui::KeyKind::Ok, fui::StateNormal, fui::QWERTY_KEY_ENTER}};
constexpr fui::KeyboardRow kKeypadRows[] = {{kKeypadRow1, 3}, {kKeypadRow2, 3}, {kKeypadRow3, 3}, {kKeypadRow4, 3}};
constexpr fui::KeyboardLayout kKeypadLayout{kKeypadRows, 4};
constexpr int kKeypadRowCount = 4;
// A key a thumb hits without looking, whatever the theme's list row height is.
constexpr int16_t kKeypadKeyHeight = 72;

UiStatusActivity::UiStatusActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity(name, renderer, mappedInput), UiAppHost(renderer) {}

void UiStatusActivity::buildCentredLines(UiScreen& screen, const StatusView& view) {
  const auto& theme = screen.theme();
  auto& target = screen.frame().target();

  const int16_t headlineHeight = target.lineHeight(theme.bodyText.font);
  const int16_t lineHeight = target.lineHeight(theme.smallText.font);
  const int16_t gap = theme.listRowGap > 0 ? theme.listRowGap : 4;
  constexpr int16_t kProgressHeight = 6;

  int lineCount = 0;
  for (size_t i = 0; i < MAX_LINES; ++i) {
    if (view.lines[i] != nullptr && view.lines[i][0] != '\0') ++lineCount;
  }

  int qrLineCount = 0;
  for (size_t i = 0; i < MAX_LINES; ++i) {
    if (view.qrLines[i] != nullptr && view.qrLines[i][0] != '\0') ++qrLineCount;
  }
  const bool hasQr = view.qrPayload != nullptr && view.qrPayload[0] != '\0';
  int16_t qrSize = hasQr ? kQrSize : 0;

  const status_stack::Metrics stack{headlineHeight, lineHeight, gap, view.showProgress ? kProgressHeight : 0};
  // The whole stack is centred as one block, so a two-line state and a
  // four-line state sit on the same middle rather than drifting up the screen.
  const fui::Rect body = screen.body();
  if (hasQr && view.qrSize > 0) {
    // A code the reader scans wants every pixel it can have, so the square is
    // capped by the body rather than by the shared default.
    qrSize = static_cast<int16_t>(std::min({view.qrSize, static_cast<int>(body.width), static_cast<int>(body.height)}));
  }
  // The band is finger-sized whatever the theme's row height is: a capsule you
  // drag is not a row you tap.
  const int16_t sliderControl = view.showSlider ? std::max(theme.rowHeight, theme.minTouchSize) : 0;
  const int16_t sliderHeight = view.showSlider ? static_cast<int16_t>(lineHeight + theme.spaceMd + sliderControl) : 0;

  // The pad sits under the band and is centred with it as one block.
  const int16_t keypadKey = std::max({theme.rowHeight, theme.minTouchSize, kKeypadKeyHeight});
  const int16_t keypadHeight = view.showSlider && view.showKeypad && mappedInput.hasTouch()
                                   ? static_cast<int16_t>(kKeypadRowCount * keypadKey + (kKeypadRowCount - 1) * gap)
                                   : 0;
  const int16_t bandBlock =
      keypadHeight > 0 ? static_cast<int16_t>(sliderHeight + gap * 2 + keypadHeight) : sliderHeight;
  const status_stack::Content content{lineCount, qrSize, qrLineCount, bandBlock, view.showProgress};
  int16_t y =
      view.linesAtTop ? body.y : static_cast<int16_t>(status_stack::topFor(stack, body.y, body.height, content));

  // The band leads: its caption already carries the readout, so the lines under
  // it are the ones explaining what moves it.
  if (view.showSlider) {
    buildSlider(screen, view, fui::Rect{body.x, y, body.width, sliderHeight});
    y = static_cast<int16_t>(y + sliderHeight + gap * 2);
  }
  if (keypadHeight > 0) {
    buildKeypad(screen, fui::Rect{body.x, y, body.width, keypadHeight});
    y = static_cast<int16_t>(y + keypadHeight + gap * 2);
  }

  int placed = 0;
  for (size_t i = 0; i < MAX_LINES; ++i) {
    if (view.lines[i] == nullptr || view.lines[i][0] == '\0') continue;
    // With a band above them the lines are all captions to it, so none of them
    // takes the headline face.
    const bool headline = placed == 0 && !view.showSlider;
    fui::TextStyle style = headline ? theme.bodyText : theme.smallText;
    style.align = fui::TextAlign::Center;
    const int16_t height = headline ? headlineHeight : lineHeight;
    if (placed > 0) y = static_cast<int16_t>(y + gap);
    target.text(fui::Rect{body.x, y, body.width, height}, view.lines[i], style);
    y = static_cast<int16_t>(y + height);
    ++placed;
  }

  if (qrSize > 0) {
    // The code, then whatever address it carries, under the lines that told the
    // reader to point a phone at it.
    if (placed > 0) y = static_cast<int16_t>(y + gap * 2);
    placeQr(0, Rect{body.x + (body.width - qrSize) / 2, y, qrSize, qrSize}, view.qrPayload);
    y = static_cast<int16_t>(y + qrSize);
    for (size_t i = 0; i < MAX_LINES; ++i) {
      if (view.qrLines[i] == nullptr || view.qrLines[i][0] == '\0') continue;
      y = static_cast<int16_t>(y + gap);
      fui::TextStyle qrStyle = theme.smallText;
      qrStyle.align = fui::TextAlign::Center;
      target.text(fui::Rect{body.x, y, body.width, lineHeight}, view.qrLines[i], qrStyle);
      y = static_cast<int16_t>(y + lineHeight);
    }
  }

  if (view.showProgress) {
    // Half the body, so the bar reads as a measure and not as a rule across the
    // page.
    const int16_t width = static_cast<int16_t>(body.width / 2);
    drawProgress(screen, view,
                 fui::Rect{static_cast<int16_t>(body.x + (body.width - width) / 2), static_cast<int16_t>(y + gap * 2),
                           width, kProgressHeight});
  }
}

// The band itself, in the one slider look (components/SliderLook.h): the name above,
// then the track, the number and the boxed minus and plus laid out like a light-panel
// row, so every slider in the firmware is the same control.
void UiStatusActivity::buildSlider(UiScreen& screen, const StatusView& view, const fui::Rect& rect) {
  const auto& theme = screen.theme();
  auto& target = screen.frame().target();
  const int16_t lineH = target.lineHeight(theme.bodyText.font);
  if (view.sliderLabel) target.text(fui::Rect{rect.x, rect.y, rect.width, lineH}, view.sliderLabel, theme.bodyText);

  const int bandY = rect.y + lineH + theme.spaceMd;
  const int bandH = rect.y + rect.height - bandY;
  if (bandH <= 0) return;
  // The number takes what it needs, never less than the panel's column, so "3 min"
  // fits as well as "7".
  const int valueW = std::max(
      light_panel::kValueWidth,
      view.sliderValueText ? target.measureText(theme.bodyText.font, view.sliderValueText, theme.bodyText).width : 0);
  const int plusX = rect.x + rect.width - light_panel::kStepWidth;
  const int minusX = plusX - light_panel::kStepGap - light_panel::kStepWidth;
  const int valueX = minusX - light_panel::kStepsGap - valueW;
  const int trackW = valueX - light_panel::kValueGap - rect.x;
  const int trackY = bandY + (bandH - slider_look::kTrackHeight) / 2;
  const int range = view.sliderMax > view.sliderMin ? view.sliderMax - view.sliderMin : 1;

  if (trackW > 0) {
    slider_look::drawTrack(renderer, rect.x, trackY, trackW, slider_look::kTrackHeight,
                           view.sliderValue - view.sliderMin, range, true);
  }
  if (view.sliderValueText) {
    target.text(fui::Rect{static_cast<int16_t>(valueX), static_cast<int16_t>(bandY), static_cast<int16_t>(valueW),
                          static_cast<int16_t>(bandH)},
                view.sliderValueText, theme.bodyText);
  }
  slider_look::drawStepper(renderer, minusX, bandY, light_panel::kStepWidth, bandH, /*plus=*/false, true);
  slider_look::drawStepper(renderer, plusX, bandY, light_panel::kStepWidth, bandH, /*plus=*/true, true);

  // Keys-only boards draw the same control and step it with the side buttons.
  if (!mappedInput.hasTouch()) return;
  auto& frame = screen.frame();
  const auto box = [](const int x, const int y, const int w, const int h) {
    return fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w),
                     static_cast<int16_t>(h)};
  };
  // The whole band height takes the touch, so the track is a finger-sized target; a tap
  // anywhere along it jumps there, and a drag follows the finger.
  if (trackW > 0) {
    frame.hit(box(rect.x, bandY, trackW, bandH), ACTION_SLIDER, 0, fui::InputTouch | fui::InputDrag);
  }
  frame.hit(box(minusX, bandY, light_panel::kStepWidth, bandH), ACTION_SLIDER, static_cast<int16_t>(-sliderStep()),
            fui::InputTouch);
  frame.hit(box(plusX, bandY, light_panel::kStepWidth, bandH), ACTION_SLIDER, static_cast<int16_t>(sliderStep()),
            fui::InputTouch);
}

void UiStatusActivity::buildKeypad(UiScreen& screen, const fui::Rect& rect) {
  // Three keys wide, each the width of a comfortable thumb, centred: a pad stretched
  // across a portrait panel would put 6 a long reach from 4.
  const int16_t width =
      std::min<int16_t>(rect.width, static_cast<int16_t>(rect.height * 3 * 3 / (2 * kKeypadRowCount)));
  fui::KeyboardProps props;
  props.layout = &kKeypadLayout;
  props.keyAction = ACTION_KEYPAD;
  props.deleteAction = ACTION_KEYPAD;
  // OK rides the pad's own action: the screen's accept is two-tap gated, the pad is not.
  props.okAction = ACTION_KEYPAD;
  props.okLabel = tr(STR_OK_BUTTON);
  props.inputMask = fui::InputTouch;
  props.labelText.font = fui::GfxRendererTarget::FONT_BODY;
  props.keyStyles = fui::outlinedButtonStyles();
  props.padding = fui::Insets{0, 0, 0, 0};
  props.gap = screen.theme().listRowGap > 0 ? screen.theme().listRowGap : 4;
  fui::keyboard(screen.frame(),
                fui::Rect{static_cast<int16_t>(rect.x + (rect.width - width) / 2), rect.y, width, rect.height}, props);
}

void UiStatusActivity::buildSections(UiScreen& screen, const StatusView& view) {
  const auto& theme = screen.theme();
  auto& target = screen.frame().target();

  const int16_t headingHeight = target.lineHeight(theme.bodyText.font);
  const int16_t lineHeight = target.lineHeight(theme.smallText.font);
  const int16_t gap = theme.listRowGap > 0 ? theme.listRowGap : 4;
  constexpr int16_t kProgressHeight = 6;

  // Instructions are read in order, so they start at the top of the body and
  // run down it; centring a list of steps only makes the eye hunt for step one.
  const fui::Rect body = screen.body();
  int16_t y = body.y;

  for (size_t s = 0; s < MAX_SECTIONS; ++s) {
    const Section& section = view.sections[s];
    const bool hasHeading = section.heading != nullptr && section.heading[0] != '\0';
    const bool hasParagraph = section.paragraph != nullptr && section.paragraph[0] != '\0';
    if (!hasHeading && !hasParagraph) continue;
    if (y > body.y) y = static_cast<int16_t>(y + gap * 2);
    if (hasHeading) {
      fui::TextStyle heading = theme.bodyText;
      heading.align = fui::TextAlign::Left;
      target.text(fui::Rect{body.x, y, body.width, headingHeight}, section.heading, heading);
      y = static_cast<int16_t>(y + headingHeight + gap);
    }

    if (hasParagraph) {
      // The block is measured with the same wrap the target draws with, so the
      // next section starts under the last line rather than under a reserved
      // worst case.
      const uint8_t maxLines = static_cast<uint8_t>(section.paragraphMaxLines > 0 ? section.paragraphMaxLines : 1);
      const int drawn = static_cast<int>(
          renderer.wrappedText(uiScaleSpec().smallFontId, section.paragraph, body.width, maxLines).size());
      const int16_t height = static_cast<int16_t>(lineHeight * (drawn > 0 ? drawn : 1));
      fui::TextStyle style = theme.smallText;
      style.align = fui::TextAlign::Left;
      style.maxLines = maxLines;
      target.text(fui::Rect{body.x, y, body.width, height}, section.paragraph, style);
      y = static_cast<int16_t>(y + height);
      continue;
    }

    // With a code, the section's lines stand beside it rather than under it: the
    // address is what the code says, so the two belong on one row.
    const bool hasCode = section.qrPayload != nullptr && section.qrPayload[0] != '\0';
    if (hasCode) placeQr(s, Rect{body.x, y, kQrSize, kQrSize}, section.qrPayload);
    const int16_t textX = hasCode ? static_cast<int16_t>(body.x + kQrSize + gap * 2) : body.x;
    const int16_t textWidth = static_cast<int16_t>(body.width - (textX - body.x));
    int16_t textY = y;
    if (hasCode) {
      int count = 0;
      for (size_t i = 0; i < MAX_LINES; ++i) {
        if (section.lines[i] != nullptr && section.lines[i][0] != '\0') ++count;
      }
      // Beside a code the lines centre on it, so a one-line address does not sit
      // at the top of a 198 px square.
      textY = static_cast<int16_t>(y + (kQrSize - count * lineHeight) / 2);
    }
    for (size_t i = 0; i < MAX_LINES; ++i) {
      if (section.lines[i] == nullptr || section.lines[i][0] == '\0') continue;
      fui::TextStyle style = theme.smallText;
      style.align = fui::TextAlign::Left;
      target.text(fui::Rect{textX, textY, textWidth, lineHeight}, section.lines[i], style);
      textY = static_cast<int16_t>(textY + lineHeight);
    }
    y = static_cast<int16_t>(hasCode ? y + kQrSize : textY);
  }

  if (view.progressLabel != nullptr && view.progressLabel[0] != '\0') {
    y = static_cast<int16_t>(y + gap * 2);
    fui::TextStyle style = theme.smallText;
    style.align = fui::TextAlign::Left;
    target.text(fui::Rect{body.x, y, body.width, lineHeight}, view.progressLabel, style);
    y = static_cast<int16_t>(y + lineHeight + gap);
  }
  if (view.showProgress) {
    drawProgress(screen, view, fui::Rect{body.x, y, body.width, kProgressHeight});
  }
}

void UiStatusActivity::placeQr(const size_t index, const Rect& rect, const char* payload) {
  if (index >= MAX_SECTIONS || payload == nullptr) return;
  qrPlacements_[index] = QrPlacement{rect, payload};
}

void UiStatusActivity::drawQrCodes() const {
  for (const QrPlacement& placement : qrPlacements_) {
    if (placement.payload == nullptr) continue;
    QrUtils::drawQrCode(renderer, placement.rect, placement.payload);
  }
}

void UiStatusActivity::drawSignal(const StatusView& view, const int bandRight, const int bandBottom) const {
  const signal_meter::Rect icon = signal_meter::iconRect(bandRight, bandBottom);
  if (!view.signalConnected) {
    // No link: a cross the size of the icon, so the band never reads as full
    // bars when nothing is connected.
    renderer.drawLine(icon.x, icon.y, icon.x + signal_meter::kHeight, icon.y + signal_meter::kHeight, 2, true);
    renderer.drawLine(icon.x, icon.y + signal_meter::kHeight, icon.x + signal_meter::kHeight, icon.y, 2, true);
    return;
  }
  for (int i = 0; i < signal_meter::kBarCount; ++i) {
    const signal_meter::Rect bar = signal_meter::barRect(icon, i);
    if (signal_meter::barIsFilled(i, view.signalBars)) {
      renderer.fillRect(bar.x, bar.y, bar.width, bar.height, true);
    } else {
      renderer.drawRect(bar.x, bar.y, bar.width, bar.height, true);
    }
  }
}

void UiStatusActivity::drawProgress(UiScreen& screen, const StatusView& view, const fui::Rect& rect) {
  fui::ProgressBarProps bar;
  bar.value = view.progressValue;
  bar.max = view.progressMax > 0 ? view.progressMax : 100;
  bar.border = fui::Paint::solid(fui::Color::Black);
  bar.borderWidth = 2;
  bar.radius = static_cast<uint8_t>(screen.theme().controlRadius);
  fui::progressBar(screen.frame(), rect, bar);
}
