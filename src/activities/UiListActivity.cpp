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
  {
    // The render task reads nav mid-build (syncToProps, layout feedback); a
    // press landing during a render would otherwise tear selection/viewport.
    RenderLock lock(*this);
    auto& n = activeNav();
    n.selected = index;
    n.follow(listCount());
  }
  requestUpdate();
}

void UiListActivity::loop() {
  if (handleCustomInput()) return;
  if (handleButtons()) return;
  if (routeListTouch()) return;

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
  applyInvertedSectionHeaderStyle(props, screen.theme());
  activeNav().syncToProps(screen.body(), rowHeight, screen.theme().listRowGap, listCount(), props);
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
  drawFooter();
  if (drawOverlay()) return;
  renderer.displayBuffer(refreshMode());
}
