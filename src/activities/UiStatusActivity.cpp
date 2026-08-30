#include "UiStatusActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
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
  const auto& theme = screen.theme();
  auto& target = screen.frame().target();

  // The header is painted outside the app (GUI.drawHeader, same as every list
  // screen), so the body starts under its reserve.
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int16_t bodyTop = static_cast<int16_t>(metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing);
  screen.setContentMargin(fui::Insets{bodyTop, static_cast<int16_t>(metrics.listSidePadding),
                                      static_cast<int16_t>(metrics.buttonHintsHeight),
                                      static_cast<int16_t>(metrics.listSidePadding)});

  const int16_t headlineHeight = target.lineHeight(theme.bodyText.font);
  const int16_t lineHeight = target.lineHeight(theme.smallText.font);
  const int16_t gap = theme.listRowGap > 0 ? theme.listRowGap : 4;
  constexpr int16_t kProgressHeight = 6;

  int16_t stackHeight = 0;
  int drawn = 0;
  for (size_t i = 0; i < MAX_LINES; ++i) {
    if (view.lines[i] == nullptr || view.lines[i][0] == '\0') continue;
    stackHeight = static_cast<int16_t>(stackHeight + (drawn == 0 ? headlineHeight : lineHeight) + (drawn ? gap : 0));
    ++drawn;
  }
  if (view.showProgress) stackHeight = static_cast<int16_t>(stackHeight + gap * 2 + kProgressHeight);

  // The whole stack is centred as one block, so a two-line state and a
  // four-line state sit on the same middle rather than drifting up the screen.
  const fui::Rect body = screen.body();
  int16_t y = static_cast<int16_t>(body.y + (body.height - stackHeight) / 2);
  if (y < body.y) y = body.y;

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
    fui::ProgressBarProps bar;
    bar.value = view.progressValue;
    bar.max = view.progressMax > 0 ? view.progressMax : 100;
    bar.border = fui::Paint::solid(fui::Color::Black);
    bar.borderWidth = 1;
    bar.radius = static_cast<uint8_t>(theme.controlRadius);
    // A quarter of the body, so the bar reads as a measure and not as a rule
    // across the page.
    const int16_t width = static_cast<int16_t>(body.width / 2);
    fui::progressBar(screen.frame(),
                     fui::Rect{static_cast<int16_t>(body.x + (body.width - width) / 2),
                               static_cast<int16_t>(y + gap * 2), width, kProgressHeight},
                     bar);
  }
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
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, view.title);
  }
  renderUi();

  const auto labels = mappedInput.mapLabels(view.backHint ? view.backHint : tr(STR_BACK),
                                            view.confirmHint ? view.confirmHint : "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (drawOverlay()) return;
  renderer.displayBuffer();
}
