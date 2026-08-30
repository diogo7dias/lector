#include "ClockOffsetActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/OffsetFieldRow.h"
#include "components/UITheme.h"

namespace {
constexpr uint8_t MAX_POS_HOURS = 14;
constexpr uint8_t MAX_NEG_HOURS = 12;
constexpr uint8_t MINUTE_STEPS = 4;  // 0, 15, 30, 45
constexpr uint8_t MINUTES_PER_QUARTER = 15;
constexpr uint8_t BIAS_QUARTER_HOURS = 48;  // 0 stored = UTC-12, 48 stored = UTC+0

// Convert a (sign, hours, quarter) triple into the biased storage value.
// Returns a value in [0, 104].
uint8_t encodeOffset(uint8_t sign, uint8_t hours, uint8_t quarter) {
  int signedQuarter = static_cast<int>(hours) * 4 + static_cast<int>(quarter);
  if (sign == 1) signedQuarter = -signedQuarter;
  int biased = signedQuarter + BIAS_QUARTER_HOURS;
  if (biased < 0) biased = 0;
  if (biased > 104) biased = 104;
  return static_cast<uint8_t>(biased);
}

// Decompose the biased storage value into (sign, hours, quarter).
void decodeOffset(uint8_t biased, uint8_t& sign, uint8_t& hours, uint8_t& quarter) {
  if (biased > 104) biased = BIAS_QUARTER_HOURS;
  int signedQuarter = static_cast<int>(biased) - BIAS_QUARTER_HOURS;
  if (signedQuarter < 0) {
    sign = 1;
    signedQuarter = -signedQuarter;
  } else {
    sign = 0;
  }
  hours = static_cast<uint8_t>(signedQuarter / 4);
  quarter = static_cast<uint8_t>(signedQuarter % 4);
}

}  // namespace

void ClockOffsetActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(ACTION_FIELD, &ClockOffsetActivity::fieldTrampoline, this);
  app.setScreen(&ClockOffsetActivity::screenTrampoline, this);
  loadFromSettings();
  activeField = FIELD_HOURS;
  refreshFieldText();
  refreshPreview();
  requestUpdate();
}

void ClockOffsetActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<ClockOffsetActivity*>(user)->buildScreen(screen);
}

void ClockOffsetActivity::fieldTrampoline(const freeink::ui::ActionEvent& event, void* user) {
  static_cast<ClockOffsetActivity*>(user)->onFieldTapped(event.value);
}

// A tap moves the edit to the field it landed on; a tap on the field already
// being edited steps it, so a touch alone can set the offset.
void ClockOffsetActivity::onFieldTapped(const int field) {
  if (field < 0 || field >= FIELD_COUNT) return;
  if (activeField == static_cast<Field>(field)) {
    adjustActiveField(+1);
  } else {
    activeField = static_cast<Field>(field);
  }
  refreshFieldText();
  refreshPreview();
  requestUpdate();
}

void ClockOffsetActivity::refreshFieldText() {
  signText[0] = sign == 1 ? '-' : '+';
  snprintf(hoursText, sizeof(hoursText), "%d", hours);
  snprintf(minutesText, sizeof(minutesText), "%02d", minutesQuarter * MINUTES_PER_QUARTER);
}

void ClockOffsetActivity::refreshPreview() {
  previewLine.clear();
  if (!halClock.isAvailable()) return;
  char timeBuf[9];
  const uint8_t encoded = encodeOffset(sign, hours, minutesQuarter);
  if (!halClock.formatTime(timeBuf, sizeof(timeBuf), encoded, SETTINGS.clockFormat == 1)) return;
  // 24 bytes did not even hold the label itself once translated: STR_CURRENT_TIME
  // is 26 bytes in Russian, 24 in Arabic and Ukrainian. See ClockSyncActivity.
  char preview[64];
  snprintf(preview, sizeof(preview), "%s %s", tr(STR_CURRENT_TIME), timeBuf);
  previewLine = preview;
}

void ClockOffsetActivity::onExit() {
  saveToSettings();
  Activity::onExit();
}

void ClockOffsetActivity::loadFromSettings() {
  decodeOffset(SETTINGS.clockUtcOffsetQ, sign, hours, minutesQuarter);
  clampForSign();
}

void ClockOffsetActivity::saveToSettings() const {
  const uint8_t encoded = encodeOffset(sign, hours, minutesQuarter);
  if (encoded == SETTINGS.clockUtcOffsetQ) return;
  SETTINGS.clockUtcOffsetQ = encoded;
  SETTINGS.saveToFile();
}

void ClockOffsetActivity::clampForSign() {
  const uint8_t maxHours = (sign == 1) ? MAX_NEG_HOURS : MAX_POS_HOURS;
  if (hours > maxHours) hours = maxHours;
  // At the absolute boundary (-12:00 or +14:00) only :00 is valid.
  if (hours == maxHours && minutesQuarter != 0) {
    minutesQuarter = 0;
  }
}

void ClockOffsetActivity::adjustActiveField(int delta) {
  switch (activeField) {
    case FIELD_SIGN: {
      sign = static_cast<uint8_t>((sign + 1) % 2);
      clampForSign();
      break;
    }
    case FIELD_HOURS: {
      const uint8_t maxHours = (sign == 1) ? MAX_NEG_HOURS : MAX_POS_HOURS;
      const int next = (static_cast<int>(hours) + delta + (maxHours + 1)) % (maxHours + 1);
      hours = static_cast<uint8_t>(next);
      clampForSign();
      break;
    }
    case FIELD_MINUTES: {
      // At the boundary hour, lock minutes to :00.
      const uint8_t maxHours = (sign == 1) ? MAX_NEG_HOURS : MAX_POS_HOURS;
      if (hours == maxHours) {
        minutesQuarter = 0;
        break;
      }
      const int next = (static_cast<int>(minutesQuarter) + delta + MINUTE_STEPS) % MINUTE_STEPS;
      minutesQuarter = static_cast<uint8_t>(next);
      break;
    }
    default:
      break;
  }
}

void ClockOffsetActivity::loop() {
  const auto route = UiAppHost::routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) return;

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    activeField = static_cast<Field>((activeField + 1) % FIELD_COUNT);
    requestUpdate();
    return;
  }

  const auto step = [this](const int delta) {
    adjustActiveField(delta);
    refreshFieldText();
    refreshPreview();
    requestUpdate();
  };
  buttonNavigator.onNextStep([&step] { step(+1); });
  buttonNavigator.onPreviousStep([&step] { step(-1); });
  buttonNavigator.onNextContinuous([&step] { step(+1); });
  buttonNavigator.onPreviousContinuous([&step] { step(-1); });
}

void ClockOffsetActivity::buildScreen(UiScreen& screen) {
  const auto& theme = screen.theme();
  auto& target = screen.frame().target();
  const auto& metrics = UITheme::getInstance().getMetrics();

  screen.setContentMargin(freeink::ui::Insets{
      static_cast<int16_t>(metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing),
      static_cast<int16_t>(metrics.contentSidePadding), static_cast<int16_t>(metrics.buttonHintsHeight),
      static_cast<int16_t>(metrics.contentSidePadding)});

  freeink::ui::TextStyle bodyStyle = theme.bodyText;
  bodyStyle.align = freeink::ui::TextAlign::Left;
  const auto widthOf = [&](const char* text) { return target.measureText(bodyStyle.font, text, bodyStyle).width; };

  // Each box is sized for the widest value it can hold, so stepping through the
  // values never moves the row.
  const int padding = theme.spaceMd;
  offset_field_row::Widths widths;
  widths.label = widthOf("UTC");
  widths.sign = std::max(widthOf("+"), widthOf("-")) + padding * 2;
  widths.hours = std::max(widthOf("14"), widthOf("12")) + padding * 2;
  widths.colon = widthOf(":");
  widths.minutes = std::max({widthOf("00"), widthOf("15"), widthOf("30"), widthOf("45")}) + padding * 2;
  const offset_field_row::Gaps gaps{theme.spaceLg, theme.spaceMd, theme.spaceSm};

  const freeink::ui::Rect body = screen.body();
  const int16_t fieldHeight = std::max(theme.rowHeight, screen.frame().device().minTouchSize);
  const int16_t lineHeight = target.lineHeight(theme.smallText.font);
  // The row and its preview centre together, so the block does not sit high on
  // the screen when the clock has nothing to preview.
  const int blockHeight = fieldHeight + (previewLine.empty() ? 0 : theme.spaceLg + lineHeight);
  const int16_t rowY = static_cast<int16_t>(body.y + std::max(0, (body.height - blockHeight) / 2));

  const offset_field_row::Row row = offset_field_row::layout(widths, gaps, body.x, body.width, rowY, fieldHeight);

  freeink::ui::TextStyle centred = bodyStyle;
  centred.align = freeink::ui::TextAlign::Center;
  target.text(freeink::ui::Rect{static_cast<int16_t>(row.label.x), static_cast<int16_t>(row.label.y),
                                static_cast<int16_t>(row.label.width), static_cast<int16_t>(row.label.height)},
              "UTC", centred);
  target.text(freeink::ui::Rect{static_cast<int16_t>(row.colon.x), static_cast<int16_t>(row.colon.y),
                                static_cast<int16_t>(row.colon.width), static_cast<int16_t>(row.colon.height)},
              ":", centred);

  const auto field = [&](const offset_field_row::Rect& rect, const char* text, const Field which) {
    freeink::ui::ButtonProps props;
    props.label = text;
    props.action = ACTION_FIELD;
    props.value = static_cast<int16_t>(which);
    props.state = activeField == which ? freeink::ui::StateSelected : freeink::ui::StateNormal;
    props.text = theme.bodyText;
    props.styles = theme.button;
    props.radius = static_cast<uint8_t>(theme.controlRadius);
    props.minTouchSize = screen.frame().device().minTouchSize;
    screen.button(props, freeink::ui::Rect{static_cast<int16_t>(rect.x), static_cast<int16_t>(rect.y),
                                           static_cast<int16_t>(rect.width), static_cast<int16_t>(rect.height)});
  };
  field(row.sign, signText, FIELD_SIGN);
  field(row.hours, hoursText, FIELD_HOURS);
  field(row.minutes, minutesText, FIELD_MINUTES);

  // The wall clock the offset produces, so it can be checked against a watch.
  if (!previewLine.empty()) {
    freeink::ui::TextStyle preview = theme.smallText;
    preview.align = freeink::ui::TextAlign::Center;
    target.text(freeink::ui::Rect{body.x, static_cast<int16_t>(rowY + fieldHeight + theme.spaceLg), body.width,
                                  lineHeight},
                previewLine.c_str(), preview);
  }
}

void ClockOffsetActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_CLOCK_UTC_OFFSET));
  renderUi();

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_NEXT_FIELD), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
