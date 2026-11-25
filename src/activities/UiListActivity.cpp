#include "UiListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "fontIds.h"
#include "util/ListIndex.h"

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
  nav.reset();
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
  const ListChrome listChrome = self->paintedChrome();
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
  self->nav.selected = event.value;
}

void UiListActivity::onRowAction(const fui::ActionEvent& event) {
  nav.selected = event.value;
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
    const int selected = nav.selected;
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
  auto& n = nav;
  n.selected = index;
  n.followOnBuild = true;  // the next build pulls the viewport to it
  requestUpdate();
}

void UiListActivity::scrollPage(const int direction) {
  bool moved = false;
  {
    // Same nav-vs-render race as moveSelectionTo: the render task writes
    // pageRows/top mid-build, so read and mutate under one lock.
    RenderLock lock(*this);
    const auto window = list_index::paged(nav.top, nav.selected, listCount(), nav.pageRows(), direction);
    moved = window.top != nav.top;
    nav.top = window.top;
    nav.selected = window.selected;
  }
  if (moved) requestUpdate();
}

void UiListActivity::loop() {
  if (handleCustomInput() || handleButtons() || routeListTouch()) return;

  // A swipe moves the window a whole screen and carries the selection along. Caught
  // here, before navigateButtons(), so a flick and a slow drag take the same path on
  // every list, including the screens that override navigateButtons().
  const auto scroll = mappedInput.wasListScrollSwipe();
  if (scroll != list_swipe::Scroll::None) {
    scrollPage(scroll == list_swipe::Scroll::PageDown ? 1 : -1);
    return;
  }
  // The touch band's page pair (ListChrome.cpp) scrolls the way a swipe does. Left and
  // Right reach a touch board only from that band.
  if (mappedInput.hasTouch()) {
    const bool up = mappedInput.wasPressed(MappedInputManager::Button::Left);
    if (up || mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      mappedInput.suppressHeldButtonRelease();
      scrollPage(up ? -1 : 1);
      return;
    }
  }

  navigateButtons();
}

void UiListActivity::navigateButtons() {
  auto& n = nav;
  buttonNavigator.onListNav(
      n.selected, listCount(), [this](const int index) { moveSelectionTo(index); },
      [this](const int direction) { scrollPage(direction); });
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
  nav.syncToProps(body, rowHeight, screen.theme().listRowGap, listCount(), props);
  if (!mappedInput.showsSelection()) {
    // Hidden selection: no row is painted selected, and the list's after-layout follow
    // check must not chase a row it was told is not there (it would scroll the list).
    props.selectedIndex = -1;
    nav.followPending = false;
  }
}

void UiListActivity::drawScrollArrows() {
  // The SDK draws no track (listScrollWidth is 0). The two chevrons sit in the
  // vertical spacing the chrome leaves above and below the rows, so they never
  // land on a row, selected or not. drawnRows is what list() really fitted, so
  // the predicate is right when wrapped rows fit fewer than the estimate.
  const auto& n = nav;
  const list_scrollbar::Arrows arrows = list_scrollbar::forWindow(listCount(), n.top, n.pageRows());
  if (!arrows.up && !arrows.down) return;
  GUI.drawScrollArrows(
      renderer, list_scrollbar::outsideBand(listBand, UITheme::getInstance().getMetrics().verticalSpacing), arrows);
}

ListChrome UiListActivity::chrome() const {
  ListChrome chrome;
  chrome.title = headerTitle();
  return chrome;
}

ListChrome UiListActivity::paintedChrome() const {
  ListChrome painted = chrome();
  const list_scrollbar::Page page = list_scrollbar::pageOf(listCount(), nav.top, nav.pageRows());
  painted.scrolls = page.pages > 0;
  if (painted.scrolls && painted.headerRight == nullptr) {
    snprintf(pageText, sizeof(pageText), "%d/%d", page.page, page.pages);
    painted.headerRight = pageText;
  }
  return painted;
}

void UiListActivity::render(RenderLock&&) {
  renderer.clearScreen();
  // The header after the rows: the page counter in it reads the viewport this build settled.
  renderUi();
  drawListChromeTop(renderer, paintedChrome());
  // Wrapped labels grow rows, so fewer rows can fit than the fixed-height
  // estimate ListNav plans with. list() reports the real layout back
  // (ListNav::onListRendered); when the selection landed past the drawn rows
  // the nav advanced the viewport and asked for another build. Bounded: top
  // strictly advances toward the selection each pass.
  for (int pass = 0; nav.consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    renderUi();
    drawListChromeTop(renderer, paintedChrome());
  }
  drawScrollArrows();
  drawListChromeBottom(renderer, mappedInput, paintedChrome());
  if (drawOverlay()) return;
  // A popup or banner painted over this screen leaves its band on the panel, so the
  // first paint after it leaves is a clean one. drawOverlay() returning above means
  // this screen painted the overlay itself, and that clean pass is still owed.
  const bool overlayDirty = UITheme::consumeOverlayPainted();
  const bool clean = cleanPaint_ || overlayDirty;
  renderer.displayBuffer(clean ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  cleanPaint_ = false;
}
