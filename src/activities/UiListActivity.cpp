#include "UiListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/ContentsLook.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "fontIds.h"

namespace fui = freeink::ui;

UiListActivity::UiListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                               const bool wantsTouchLongPress)
    : Activity(name, renderer, mappedInput), UiAppHost(renderer), wantsTouchLongPress(wantsTouchLongPress) {}

void UiListActivity::onEnter() {
  Activity::onEnter();
  // Before resetUi(): the shared theme tokens are derived from this target's
  // fonts, so a screen-specific body font has to be bound first.
  const int fontId = listFontId();
  if (fontId != 0) uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, fontId);
  contents_look::bindFonts(uiTarget);
  activeNav().reset();
  resetUi();
  app.on(ACTION_ROW, &UiListActivity::rowActionTrampoline, this);
  setArmHook(&UiListActivity::rowArmTrampoline, this);
  app.setScreen(&UiListActivity::screenTrampoline, this);
  requestUpdate();
}

void UiListActivity::screenTrampoline(UiScreen& screen, void* user) {
  auto* self = static_cast<UiListActivity*>(user);
  // The body is reserved from the same bands the chrome paints, so a screen
  // cannot draw a header the list then runs under. A screen wanting a different
  // band still calls setContentMargin itself; this only sets the default.
  const ListChrome listChrome = self->shownChrome();
  const list_chrome::Bands bands = listChromeBands(self->renderer, listChrome);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(bands.contentTop),
                                      static_cast<int16_t>(listChrome.sideInset),
                                      static_cast<int16_t>(self->renderer.getScreenHeight() - bands.contentBottom),
                                      static_cast<int16_t>(listChrome.sideInset)});
  self->buildScreen(screen);
}

void UiListActivity::rowActionTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiListActivity*>(user);
  if (event.value < 0 || event.value >= self->listCount()) return;
  self->onRowAction(event);
}

void UiListActivity::rowArmTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiListActivity*>(user);
  if (event.action != ACTION_ROW) return;
  if (event.value < 0 || event.value >= self->listCount()) return;
  self->activeNav().selected = event.value;
}

void UiListActivity::onRowAction(const fui::ActionEvent& event) {
  activeNav().selected = event.value;
  if (event.longPress) {
    onRowLongPress(event.value);
    return;
  }
  activateIndex(event.value);
}

bool UiListActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBackButton();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const int selected = activeNav().selected;
    if (selected >= 0 && selected < listCount()) activateIndex(selected);
    return true;
  }
  return false;
}

bool UiListActivity::routeListTouch() {
  // Touch goes through the FreeInkApp: render() registered the row hit rects;
  // route the snapshot and let the action trampoline dispatch.
  const auto route = UiAppHost::routeTouch(mappedInput, wantsTouchLongPress);
  // No pressed-state repaint: the render it triggers would drop a slow tap's
  // release inside the uiReady window (tap-to-activate needed two taps), and
  // it costs a second e-ink refresh per tap.
  if (route.routed && app.invalidated()) requestUpdate();
  return static_cast<bool>(route);  // dispatched to the action handler
}

void UiListActivity::moveSelectionTo(const int index) {
  // No render lock: `selected` is written only here, on the main task, and the
  // viewport pull is deferred to the next build, where ListNav::syncToProps
  // consumes followOnBuild. Taking the lock parked the main loop for a whole
  // e-ink refresh, and the buttons are sampled once per loop pass from level
  // state with no queue, so a press that started and ended inside that window
  // was never seen at all.
  auto& n = activeNav();
  n.selected = index;
  n.followOnBuild = true;  // the next build pulls the viewport to it
  requestUpdate();
}

void UiListActivity::loop() {
  if (handleCustomInput() || handleButtons() || routeListTouch()) return;

  // Swipes scroll the viewport; the selection stays put (it may scroll
  // off-screen) and button navigation pulls the view back to it.
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    bool moved = false;
    {
      // Same nav-vs-render race as moveSelectionTo: the render task writes
      // pageRows/top mid-build, so read and mutate under one lock.
      RenderLock lock(*this);
      auto& n = activeNav();
      const int delta = swipe == MappedInputManager::SwipeDir::Up ? n.pageRows() : -n.pageRows();
      moved = n.scrollBy(delta, listCount());
    }
    if (moved) requestUpdate();
    return;
  }

  navigateButtons();
}

void UiListActivity::navigateButtons() {
  auto& n = activeNav();
  const int count = listCount();
  const int from = n.selected;
  buttonNavigator.onListNav(from, count, n.pageRows(), [this, from, count](int index) {
    // The step's direction, wrap included: forward when the way round is shorter that way.
    const bool forward = count > 0 && (index - from + count) % count <= count / 2;
    for (int tries = 0; tries < count && isHeaderRow(index); ++tries) {
      index = forward ? (index + 1) % count : (index - 1 + count) % count;
    }
    moveSelectionTo(index);
  });
}

void UiListActivity::syncListViewport(UiScreen& screen, fui::ListProps& props, const bool hasSubtitle) {
  // Labels and subtitles wrap rather than truncate; the contents props then set the
  // faces. Fixed geometry on every board: the painter ignores the theme's row tokens.
  applyWrappingRowStyle(props, screen.theme());
  const int16_t rowHeight = contents_look::rowHeight(hasSubtitle);
  props.rowHeight = rowHeight;
  contents_look::applyListProps(props);
  // Remembered for the chevrons render() draws once the list has reported what it
  // actually laid out.
  const fui::Rect body = screen.body();
  listBand = Rect{body.x, body.y, body.width, body.height};
  activeNav().syncToProps(body, rowHeight, 0, listCount(), props);
}

void UiListActivity::drawScrollArrows() {
  // The SDK draws no track (listScrollWidth is 0). The two chevrons sit in the
  // vertical spacing the chrome leaves above and below the rows, so they never
  // land on a row, selected or not. drawnRows is what list() really fitted, so
  // the predicate is right when wrapped rows fit fewer than the estimate.
  const auto& n = activeNav();
  const list_scrollbar::Arrows arrows = list_scrollbar::forWindow(listCount(), n.top, n.pageRows());
  if (!arrows.up && !arrows.down) return;
  GUI.drawScrollArrows(
      renderer, list_scrollbar::outsideBand(listBand, UITheme::getInstance().getMetrics().verticalSpacing), arrows);
}

ListChrome UiListActivity::shownChrome() const {
  ListChrome shown = chrome();
  toContentsLook(shown, mappedInput.hasTouch());
  return shown;
}

ListChrome UiListActivity::chrome() const {
  ListChrome chrome;
  chrome.title = headerTitle();
  return chrome;
}

void UiListActivity::drawChrome() { drawListChromeTop(renderer, shownChrome()); }

void UiListActivity::drawFooter() { drawListChromeBottom(renderer, mappedInput, shownChrome()); }

void UiListActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawChrome();
  renderUi();
  // Wrapped labels grow rows, so fewer rows can fit than the fixed-height
  // estimate ListNav plans with. list() reports the real layout back
  // (ListNav::onListRendered); when the selection landed past the drawn rows
  // the nav advanced the viewport and asked for another build. Bounded: top
  // strictly advances toward the selection each pass.
  for (int pass = 0; activeNav().consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }
  drawScrollArrows();
  drawFooter();
  if (drawOverlay()) return;
  renderer.displayBuffer(refreshMode());
}
