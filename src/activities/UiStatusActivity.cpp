#include "UiStatusActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/ComparisonLayout.h"
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
  app.on(ACTION_ACCEPT, &UiStatusActivity::acceptTrampoline, this);
  app.on(ACTION_CANCEL, &UiStatusActivity::cancelTrampoline, this);
  app.on(ACTION_CHOICE, &UiStatusActivity::choiceTrampoline, this);
  app.setScreen(&UiStatusActivity::screenTrampoline, this);
  requestUpdate();
}

void UiStatusActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<UiStatusActivity*>(user)->buildScreen(screen);
}

void UiStatusActivity::acceptTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<UiStatusActivity*>(user)->onConfirmButton();
}

void UiStatusActivity::cancelTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<UiStatusActivity*>(user)->onBackButton();
}

void UiStatusActivity::choiceTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiStatusActivity*>(user);
  if (event.value < 0 || event.value >= self->choiceCount_) return;
  self->choiceIndex_ = event.value;
  self->onChoiceActivated(event.value);
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

  // Taken from the bottom first, so neither shape lays text into the band the
  // buttons stand in.
  buildActions(screen, view);

  buildChoiceBand(screen, view);

  if (view.comparison[0].label != nullptr) {
    buildComparison(screen, view);
    return;
  }
  if (view.sections[0].heading != nullptr) {
    buildSections(screen, view);
    return;
  }
  buildCentredLines(screen, view);
}

void UiStatusActivity::buildActions(UiScreen& screen, const StatusView& view) {
  const bool hasCancel = view.cancelLabel != nullptr && view.cancelLabel[0] != '\0';
  const bool hasAccept = view.acceptLabel != nullptr && view.acceptLabel[0] != '\0';
  if (!hasCancel && !hasAccept) return;

  const auto& theme = screen.theme();
  const int16_t height = static_cast<int16_t>(theme.rowHeight + theme.spaceSm * 2);
  const fui::Rect band = screen.takeBottom(height, theme.spaceSm);
  const int16_t gap = theme.spaceSm;
  const int16_t width = hasCancel && hasAccept ? static_cast<int16_t>((band.width - gap) / 2) : band.width;

  fui::ButtonProps props;
  props.text = theme.bodyText;
  props.styles = theme.button;
  props.minTouchSize = screen.frame().device().minTouchSize;
  props.radius = static_cast<uint8_t>(theme.controlRadius);
  if (hasCancel) {
    props.label = view.cancelLabel;
    props.action = ACTION_CANCEL;
    screen.button(props, fui::Rect{band.x, band.y, width, band.height});
  }
  if (hasAccept) {
    props.label = view.acceptLabel;
    props.action = ACTION_ACCEPT;
    const int16_t x = hasCancel ? static_cast<int16_t>(band.x + band.width - width) : band.x;
    screen.button(props, fui::Rect{x, band.y, width, band.height});
  }
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

void UiStatusActivity::buildChoiceBand(UiScreen& screen, const StatusView& view) {
  // A fixed pair (take theirs / send mine) or a list the screen owns (the
  // readers it found): both end up as the same rows, selected by the same
  // index, dispatched through the same action.
  const char* const* labels = view.choices[0] != nullptr ? view.choices.data() : view.choiceList;
  const int offered = view.choices[0] != nullptr ? static_cast<int>(MAX_CHOICES) : view.choiceListCount;

  int count = 0;
  for (int i = 0; i < offered && count < static_cast<int>(MAX_LIST_CHOICES); ++i) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;
    ++count;
  }
  choiceCount_ = count;
  if (choiceIndex_ >= count) choiceIndex_ = count > 0 ? count - 1 : 0;
  if (count == 0) return;

  const auto& theme = screen.theme();
  const int16_t gap = theme.listRowGap > 0 ? theme.listRowGap : 4;
  const int16_t rowHeight = theme.rowHeight;

  // Rows are copied into a buffer that outlives the build: FreeInkUI keeps the
  // interaction table pointing at what was drawn.
  for (int i = 0, placed = 0; i < offered && placed < count; ++i) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;
    choiceItems_[placed] = fui::ListItem{};
    choiceItems_[placed].label = labels[i];
    choiceItems_[placed].actionValue = static_cast<int16_t>(placed);
    ++placed;
  }

  // Never more than half the body: a long list of readers still leaves the
  // headline that says what the list is for on screen.
  const int16_t bodyHeight = screen.body().height;
  int visible = count;
  const int16_t maxBand = static_cast<int16_t>(bodyHeight / 2);
  while (visible > 1 && rowHeight * visible + gap * (visible - 1) > maxBand) --visible;

  // Scroll only as far as it takes to keep the selection on screen.
  int top = 0;
  if (choiceIndex_ >= visible) top = choiceIndex_ - visible + 1;

  const int16_t band = static_cast<int16_t>(rowHeight * visible + gap * (visible - 1));
  fui::ListProps props;
  props.items = choiceItems_.data();
  props.count = static_cast<uint16_t>(count);
  props.selectedIndex = static_cast<int16_t>(choiceIndex_);
  props.topIndex = static_cast<uint16_t>(top);
  props.action = ACTION_CHOICE;
  props.rowHeight = rowHeight;
  props.rowGap = gap;
  // list() takes the band itself; the spacer after it is the air between the
  // answers and whatever the screen draws above them.
  screen.list(props, band, fui::LayoutAnchor::Bottom);
  screen.spacer(static_cast<int16_t>(gap * 2), fui::LayoutAnchor::Bottom);
}

void UiStatusActivity::buildComparison(UiScreen& screen, const StatusView& view) {
  const auto& theme = screen.theme();
  auto& target = screen.frame().target();

  const int16_t headlineHeight = target.lineHeight(theme.bodyText.font);
  const int16_t lineHeight = target.lineHeight(theme.smallText.font);
  const int16_t gap = theme.listRowGap > 0 ? theme.listRowGap : 4;

  comparison_layout::Content content;
  content.hasHeadline = view.comparisonHeadline != nullptr && view.comparisonHeadline[0] != '\0';
  content.hasRelation = view.comparisonRelation != nullptr && view.comparisonRelation[0] != '\0';
  for (size_t side = 0; side < view.comparison.size(); ++side) {
    for (const char* line : view.comparison[side].lines) {
      if (line != nullptr && line[0] != '\0') ++content.sideLines[side];
    }
  }
  const comparison_layout::Metrics stack{headlineHeight, headlineHeight, lineHeight, gap};

  const fui::Rect body = screen.body();
  int16_t y = static_cast<int16_t>(comparison_layout::topFor(stack, body.y, body.height, content));

  if (content.hasHeadline) {
    fui::TextStyle style = theme.bodyText;
    style.align = fui::TextAlign::Center;
    target.text(fui::Rect{body.x, y, body.width, headlineHeight}, view.comparisonHeadline, style);
    y = static_cast<int16_t>(y + headlineHeight);
  }

  for (size_t side = 0; side < view.comparison.size(); ++side) {
    const ComparisonSide& block = view.comparison[side];
    if (block.label == nullptr || block.label[0] == '\0') continue;
    if (y > body.y) y = static_cast<int16_t>(y + gap * 2);
    fui::TextStyle label = theme.bodyText;
    label.align = fui::TextAlign::Left;
    target.text(fui::Rect{body.x, y, body.width, headlineHeight}, block.label, label);
    y = static_cast<int16_t>(y + headlineHeight);
    for (const char* line : block.lines) {
      if (line == nullptr || line[0] == '\0') continue;
      y = static_cast<int16_t>(y + gap);
      fui::TextStyle style = theme.smallText;
      style.align = fui::TextAlign::Left;
      target.text(fui::Rect{body.x, y, body.width, lineHeight}, line, style);
      y = static_cast<int16_t>(y + lineHeight);
    }
  }

  if (content.hasRelation) {
    y = static_cast<int16_t>(y + gap * 2);
    fui::TextStyle style = theme.smallText;
    style.align = fui::TextAlign::Left;
    target.text(fui::Rect{body.x, y, body.width, lineHeight}, view.comparisonRelation, style);
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

bool UiStatusActivity::moveChoice(const int delta) {
  if (choiceCount_ < 2) return false;
  int next = choiceIndex_ + delta;
  while (next < 0) next += choiceCount_;
  choiceIndex_ = next % choiceCount_;
  requestUpdate();
  return true;
}

void UiStatusActivity::loop() {
  if (handleCustomInput()) return;

  // The screen's own buttons, when it drew any: the interaction table the last
  // render published is what a tap is measured against.
  const auto route = UiAppHost::routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) return;

  // Any direction steps between the answers: with two of them there is no
  // meaningful difference between next and previous, and both readers reach for
  // whichever key is under the thumb.
  if (choiceCount_ > 1) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (moveChoice(-1)) return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
        mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (moveChoice(1)) return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBackButton();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (choiceCount_ > 0) {
      onChoiceActivated(choiceIndex_);
      return;
    }
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
