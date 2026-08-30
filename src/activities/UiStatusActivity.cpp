#include "UiStatusActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/StatusStack.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

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

  const status_stack::Metrics stack{headlineHeight, lineHeight, gap, view.showProgress ? kProgressHeight : 0};
  // The whole stack is centred as one block, so a two-line state and a
  // four-line state sit on the same middle rather than drifting up the screen.
  const fui::Rect body = screen.body();
  int16_t y = static_cast<int16_t>(status_stack::topFor(stack, body.y, body.height, lineCount, view.showProgress));

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
    for (size_t i = 0; i < MAX_LINES; ++i) {
      if (section.lines[i] == nullptr || section.lines[i][0] == '\0') continue;
      fui::TextStyle style = theme.smallText;
      style.align = fui::TextAlign::Left;
      target.text(fui::Rect{body.x, y, body.width, lineHeight}, section.lines[i], style);
      y = static_cast<int16_t>(y + lineHeight);
    }
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

  renderer.clearScreen();
  if (view.title) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int pageWidth = renderer.getScreenWidth();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, view.title);
    if (view.subtitleLeft) {
      GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                        view.subtitleLeft, view.subtitleRight);
    }
  }
  renderUi();

  const auto labels = mappedInput.mapLabels(view.backHint ? view.backHint : tr(STR_BACK),
                                            view.confirmHint ? view.confirmHint : "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (drawOverlay()) return;
  renderer.displayBuffer();
}
