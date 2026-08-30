#include "UiStatusActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/SignalMeter.h"
#include "components/StatusStack.h"
#include "components/UITheme.h"
#include "util/QrUtils.h"

namespace fui = freeink::ui;

// The square every QR on these screens is drawn at: big enough for a phone to
// read across a desk, small enough that two fit on a portrait panel.
constexpr int16_t kQrSize = 198;

UiStatusActivity::UiStatusActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity(name, renderer, mappedInput), UiAppHost(renderer) {}

void UiStatusActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.setScreen(&UiStatusActivity::screenTrampoline, this);
  requestUpdate();
}

void UiStatusActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<UiStatusActivity*>(user)->buildScreen(screen);
}

void UiStatusActivity::buildScreen(UiScreen& screen) {
  const StatusView view = statusView();
  qrPlacements_ = {};

  // The header (and its sub-header) are painted outside the app, same as every
  // list screen, so the body starts under whichever of them was drawn.
  const auto& metrics = UITheme::getInstance().getMetrics();
  int16_t bodyTop = static_cast<int16_t>(metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing);
  if (view.subtitleLeft) bodyTop = static_cast<int16_t>(bodyTop + metrics.tabBarHeight);
  screen.setContentMargin(fui::Insets{bodyTop, static_cast<int16_t>(metrics.contentSidePadding),
                                      static_cast<int16_t>(metrics.buttonHintsHeight),
                                      static_cast<int16_t>(metrics.contentSidePadding)});

  if (view.sections[0].heading != nullptr) {
    buildSections(screen, view);
    return;
  }
  buildCentredLines(screen, view);
}

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
  const int16_t qrSize = view.qrPayload != nullptr && view.qrPayload[0] != '\0' ? kQrSize : 0;

  const status_stack::Metrics stack{headlineHeight, lineHeight, gap, view.showProgress ? kProgressHeight : 0};
  const status_stack::Content content{lineCount, qrSize, qrLineCount, view.showProgress};
  // The whole stack is centred as one block, so a two-line state and a
  // four-line state sit on the same middle rather than drifting up the screen.
  const fui::Rect body = screen.body();
  int16_t y = static_cast<int16_t>(status_stack::topFor(stack, body.y, body.height, content));

  int placed = 0;
  for (size_t i = 0; i < MAX_LINES; ++i) {
    if (view.lines[i] == nullptr || view.lines[i][0] == '\0') continue;
    fui::TextStyle style = placed == 0 ? theme.bodyText : theme.smallText;
    style.align = fui::TextAlign::Center;
    const int16_t height = placed == 0 ? headlineHeight : lineHeight;
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
    if (section.heading == nullptr || section.heading[0] == '\0') continue;
    if (y > body.y) y = static_cast<int16_t>(y + gap * 2);
    fui::TextStyle heading = theme.bodyText;
    heading.align = fui::TextAlign::Left;
    target.text(fui::Rect{body.x, y, body.width, headingHeight}, section.heading, heading);
    y = static_cast<int16_t>(y + headingHeight + gap);

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
  bar.borderWidth = 1;
  bar.radius = static_cast<uint8_t>(screen.theme().controlRadius);
  fui::progressBar(screen.frame(), rect, bar);
}

void UiStatusActivity::loop() {
  if (handleCustomInput()) return;

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBackButton();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    onConfirmButton();
    return;
  }
}

void UiStatusActivity::render(RenderLock&&) {
  const StatusView view = statusView();
  if (view.hidden) return;

  renderer.clearScreen();
  if (view.title) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int pageWidth = renderer.getScreenWidth();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, view.title);
    if (view.subtitleLeft) {
      const int bandTop = metrics.topPadding + metrics.headerHeight;
      GUI.drawSubHeader(renderer, Rect{0, bandTop, pageWidth, metrics.tabBarHeight}, view.subtitleLeft,
                        view.subtitleRight);
      if (view.showSignal) {
        drawSignal(view, pageWidth - metrics.contentSidePadding,
                   bandTop + metrics.tabBarHeight - metrics.verticalSpacing);
      }
    }
  }
  renderUi();
  // The codes are bitmaps, not FreeInkUI elements: the body layout said where
  // they go, and they land in the same buffer the app just drew into.
  drawQrCodes();

  const auto labels = mappedInput.mapLabels(view.backHint ? view.backHint : tr(STR_BACK),
                                            view.confirmHint ? view.confirmHint : "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (drawOverlay()) return;
  renderer.displayBuffer(view.refresh);
}
