#include "UiListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "fontIds.h"
#include "util/HoldRepeat.h"

namespace fui = freeink::ui;

UiListActivity::UiListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                               const bool wantsTouchLongPress)
    : Activity(name, renderer, mappedInput), UiAppHost(renderer), wantsTouchLongPress(wantsTouchLongPress) {}

void UiListActivity::onEnter() {
  Activity::onEnter();
  // Before resetUi(): the shared theme tokens are derived from this target's
  // fonts, so a screen-specific body font has to be bound first.
  const int fontId = listFontId();
  if (fontId >= 0) uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, fontId);
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
  const ListChrome chrome = self->chrome();
  const list_chrome::Bands bands = listChromeBands(self->renderer, chrome);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(bands.contentTop), static_cast<int16_t>(chrome.sideInset),
                                      static_cast<int16_t>(self->renderer.getScreenHeight() - bands.contentBottom),
                                      static_cast<int16_t>(chrome.sideInset)});
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
  const int count = listCount();
  auto& n = activeNav();
  buttonNavigator.onNextRelease([this, count, &n] { moveSelectionTo(ButtonNavigator::nextIndex(n.selected, count)); });
  buttonNavigator.onPreviousRelease(
      [this, count, &n] { moveSelectionTo(ButtonNavigator::previousIndex(n.selected, count)); });
  // A hold travels in ROWS, not pages. Paging per repeat moved ~14 rows twice a
  // second, so a held key crossed a long list far faster than the panel could
  // show it and there was no way to stop on a row. One row per repeat at the
  // list interval is aimable, and holdRepeatStep() coarsens it to five once the
  // hold has plainly stopped being a nudge — the same ramp the numeric settings
  // use, so there is one hold feel in the firmware rather than two.
  // Clamped rather than wrapped: a hold that wraps past the end never ends.
  //
  // A swipe arrives through this same callback and stays a page: it is a travel
  // gesture, and a finger that moved one row would be useless.
  buttonNavigator.onNextContinuous([this, count, &n] {
    moveSelectionTo(ButtonNavigator::swipeDrivenPass()
                        ? ButtonNavigator::nextPageIndex(n.selected, count, n.pageRows())
                        : ButtonNavigator::heldIndex(n.selected, count, holdRepeatStep(buttonNavigator.repeats())));
  });
  buttonNavigator.onPreviousContinuous([this, count, &n] {
    moveSelectionTo(ButtonNavigator::swipeDrivenPass()
                        ? ButtonNavigator::previousPageIndex(n.selected, count, n.pageRows())
                        : ButtonNavigator::heldIndex(n.selected, count, -holdRepeatStep(buttonNavigator.repeats())));
  });
}

void UiListActivity::syncListViewport(UiScreen& screen, fui::ListProps& props, const bool hasSubtitle) {
  int16_t rowHeight = screen.theme().rowHeight;
  // Setting name and value at the same size and weight on the keys-only boards.
  // Done here rather than in each screen: every list goes through this call, so
  // one place cannot be forgotten by a new one.
  applyKeysOnlyValueStyle(props, screen.theme());
  if (!mappedInput.hasTouch()) {
    // Non-touch hardware (X3/X4) keeps the original, denser per-theme row
    // height instead of FreeInkUI's touch-target-sized default, so lists fit
    // as many rows per screen as they did before the FreeInkUI migration.
    // props.rowHeight must be set explicitly: screen.list() otherwise falls
    // back to the (touch-friendly) theme token, not this local value.
    // A label that must wrap (labelText.maxLines > 1) grows only its own row:
    // list() sizes wrapped items per-row, so the dense height stays.
    const auto& metrics = UITheme::getInstance().getMetrics();
    rowHeight = static_cast<int16_t>(hasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight);
    props.rowHeight = rowHeight;
  }
  applyWrappingRowStyle(props, screen.theme());
  applyInvertedSectionHeaderStyle(props, screen.theme());
  // Remembered for the chevrons render() draws once the list has reported what it
  // actually laid out.
  const fui::Rect body = screen.body();
  listBand = Rect{body.x, body.y, body.width, body.height};
  activeNav().syncToProps(body, rowHeight, screen.theme().listRowGap, listCount(), props);
}

void UiListActivity::drawScrollArrows() {
  // The SDK draws no track (listScrollWidth is 0). The two chevrons sit in the
  // vertical spacing the chrome leaves above and below the rows, so they never
  // land on a row, selected or not. drawnRows is what list() really fitted, so
  // the predicate is right when wrapped rows fit fewer than the estimate.
  const auto& n = activeNav();
  const list_scrollbar::Arrows arrows = list_scrollbar::forWindow(listCount(), n.top, n.pageRows());
  if (!arrows.up && !arrows.down) return;
  const int spacing = UITheme::getInstance().getMetrics().verticalSpacing;
  const int reach = list_scrollbar::kHeight + list_scrollbar::kGap;
  const Rect band{listBand.x, listBand.y - std::min(spacing, reach), listBand.width,
                  listBand.height + std::min(spacing, reach) * 2};
  GUI.drawScrollArrows(renderer, band, arrows);
}

ListChrome UiListActivity::chrome() const {
  ListChrome chrome;
  chrome.title = headerTitle();
  return chrome;
}

void UiListActivity::drawChrome() { drawListChromeTop(renderer, chrome()); }

void UiListActivity::drawFooter() { drawListChromeBottom(renderer, mappedInput, chrome()); }

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
