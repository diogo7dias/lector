#include "ReaderToolbarUi.h"

#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "ReaderToolbarSteps.h"
#include "components/UITheme.h"
#include "components/icons/readerToolbarIcons.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_DISMISS = 1;  // tap anywhere on the page above the sheet
constexpr fui::ActionId ACTION_TOOL = 2;     // value = reader_toolbar::Tool
constexpr fui::ActionId ACTION_PREV = 3;     // scrub row: previous chapter
constexpr fui::ActionId ACTION_NEXT = 4;     // scrub row: next chapter
constexpr fui::ActionId ACTION_SCRUB = 5;    // progress track: dragPermille along the book

// Scrub row: two small round-cornered chapter buttons flanking a thin progress track with a
// round knob. The reading page's chrome is light, so the controls stay slim.
constexpr int16_t SCRUB_BUTTON = 36;  // chapter step buttons (square)
constexpr int16_t SCRUB_KNOB = 16;    // round knob on the 2 px progress track
constexpr int16_t SCRUB_GAP = 12;     // air between the buttons and the track
// Tool row: a 24 px glyph centred in each slot, the focused slot in an outline pill. The
// whole slot is the tap target.
constexpr int16_t TOOL_ROW_HEIGHT = 80;
constexpr int16_t TOOL_PILL_INSET = 10;
}  // namespace

ReaderToolbarUi::ReaderToolbarUi(GfxRenderer& renderer) : UiAppHost(renderer) {}

void ReaderToolbarUi::begin() {
  resetUi();
  pending_ = Routed{};
  for (fui::ActionId id = ACTION_DISMISS; id <= ACTION_SCRUB; ++id) {
    app.on(id, &ReaderToolbarUi::onAction, this);
  }
  app.setScreen(&ReaderToolbarUi::screenFn, this);
}

ReaderToolbarUi::Routed ReaderToolbarUi::route(const MappedInputManager& input) {
  pending_ = Routed{};
  // routeHeld: the scrub track is a drag target, so held frames must reach it.
  const auto touch = routeTouch(input, false, /*routeHeld=*/true);
  pending_.routed = touch.routed;
  // Only the release commits a scrub: every held frame dispatches too (dragPermille set),
  // and re-paginating a chapter per frame would be seconds of work per swipe.
  if (pending_.event == Event::Scrub && !touch.snap.touchReleased) pending_.event = Event::None;
  return pending_;
}

void ReaderToolbarUi::onAction(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<ReaderToolbarUi*>(user);
  Routed& out = self->pending_;
  out.value = event.value;
  out.permille = event.dragPermille;
  if (event.action >= ACTION_DISMISS && event.action <= ACTION_SCRUB) out.event = static_cast<Event>(event.action);
  if (out.event == Event::Scrub && event.dragPermille < 0) out.event = Event::None;
  // A handled action repaints through the reader's own path, not through app.invalidate():
  // the page underneath is the reader's to draw.
  self->app.clearTapFlash();
}

void ReaderToolbarUi::screenFn(UiScreen& screen, void* user) {
  static_cast<ReaderToolbarUi*>(user)->buildToolbar(screen);
}

void ReaderToolbarUi::buildToolbar(UiScreen& screen) {
  const auto& tokens = screen.theme();

  // Sheet height from its content: scrub row, meta line, tool row, and the air between
  // them (a full spaceLg under the scrub row, so the track and its chapter buttons do not
  // crowd the meta line).
  const int16_t metaH = screen.target().lineHeight(tokens.smallText.font);
  const int16_t contentH = static_cast<int16_t>(tokens.spaceMd + SCRUB_BUTTON + tokens.spaceLg + metaH +
                                                tokens.spaceSm + TOOL_ROW_HEIGHT + tokens.spaceSm);
  fui::SheetProps sheetProps;
  sheetProps.anchor = fui::SheetEdge::Bottom;
  sheetProps.dismissAction = ACTION_DISMISS;
  // Grabber air matches the light panel's card language, so the two sheets read as one family.
  sheetProps.grabberMargin = tokens.spaceLg;
  sheetProps.grabberInset = static_cast<int16_t>(tokens.spaceLg + tokens.spaceMd);
  const int16_t grabberBand =
      static_cast<int16_t>(sheetProps.grabberMargin + sheetProps.grabberHeight + sheetProps.grabberInset);
  screen.sheet(sheetProps, static_cast<int16_t>(contentH + grabberBand));
  screen.insetContent(fui::Insets{0, tokens.spaceLg, 0, tokens.spaceLg});
  screen.spacer(tokens.spaceMd);

  // Scrub row: < [progress track and knob: tap or drag to jump] >
  {
    const fui::Rect band = screen.takeTop(SCRUB_BUTTON, tokens.spaceLg);
    stepProps_.label = nullptr;
    stepProps_.icon = fui::bitmapFromIcon(icon_reader_back_24);
    stepProps_.action = ACTION_PREV;
    stepProps_.inputMask = fui::InputTouch;
    stepProps_.styles.explicitlySet = true;
    stepProps_.styles.normal.background = fui::Paint::solid(fui::Color::White);
    stepProps_.styles.normal.foreground = fui::Paint::solid(fui::Color::Black);
    stepProps_.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    stepProps_.styles.normal.borderWidth = 1;
    stepProps_.styles.normal.radius = tokens.controlRadius;
    stepProps_.styles.selected = stepProps_.styles.normal;
    stepProps_.styles.focused = stepProps_.styles.normal;
    stepProps_.styles.disabled = stepProps_.styles.normal;
    stepProps_.styles.active = stepProps_.styles.normal;
    stepProps_.styles.active.background = fui::Paint::solid(fui::Color::Black);
    stepProps_.styles.active.foreground = fui::Paint::solid(fui::Color::White);
    screen.button(stepProps_, fui::Rect{band.x, band.y, SCRUB_BUTTON, SCRUB_BUTTON});
    stepProps_.icon = fui::bitmapFromIcon(icon_reader_next_24);
    stepProps_.action = ACTION_NEXT;
    screen.button(stepProps_,
                  fui::Rect{static_cast<int16_t>(band.right() - SCRUB_BUTTON), band.y, SCRUB_BUTTON, SCRUB_BUTTON});

    // Progress: a 2 px track with a solid round knob at the book position, the scrub hit
    // rect spanning the whole band height so a finger a little off the line still scrubs.
    const int16_t trackX = static_cast<int16_t>(band.x + SCRUB_BUTTON + SCRUB_GAP);
    const int16_t trackW = static_cast<int16_t>(band.width - 2 * (SCRUB_BUTTON + SCRUB_GAP));
    const int16_t trackY = static_cast<int16_t>(band.y + band.height / 2);
    const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
    screen.target().fill(fui::Rect{trackX, static_cast<int16_t>(trackY - 1), trackW, 2}, ink);
    const int permille = std::clamp(model_.progressPermille, 0, 1000);
    const int16_t knobX = static_cast<int16_t>(trackX + (static_cast<int32_t>(trackW - 1) * permille) / 1000);
    screen.target().fill(fui::Rect{static_cast<int16_t>(knobX - SCRUB_KNOB / 2),
                                   static_cast<int16_t>(trackY - SCRUB_KNOB / 2), SCRUB_KNOB, SCRUB_KNOB},
                         ink, static_cast<uint8_t>(SCRUB_KNOB / 2));
    screen.frame().hit(fui::Rect{trackX, band.y, trackW, band.height}, ACTION_SCRUB, 0,
                       fui::InputTouch | fui::InputDrag);
  }

  // Meta line: chapter title (left), chapter page and book percent (right).
  {
    const fui::Rect line = screen.takeTop(metaH, tokens.spaceSm);
    fui::TextStyle titleStyle = tokens.smallText;
    titleStyle.bold = true;
    fui::TextStyle infoStyle = tokens.smallText;
    infoStyle.align = fui::TextAlign::Right;
    const int16_t infoW =
        model_.pageInfo ? screen.target().measureText(infoStyle.font, model_.pageInfo, infoStyle).width : 0;
    const fui::Rect titleRect{line.x, line.y, static_cast<int16_t>(line.width - infoW - tokens.spaceMd), line.height};
    if (model_.chapterTitle) screen.target().text(titleRect, model_.chapterTitle, titleStyle);
    if (model_.pageInfo) screen.target().text(line, model_.pageInfo, infoStyle);
  }

  // Tool row: three equal slots, an icon centred in each, the focused one inside an outline
  // pill. Each slot is one tap target, so the row stays light (no filled tiles, no labels).
  const fui::BitmapRef icons[reader_toolbar::TOOL_COUNT] = {fui::bitmapFromIcon(icon_reader_contents_24),
                                                            fui::bitmapFromIcon(icon_reader_text_24),
                                                            fui::bitmapFromIcon(icon_reader_more_24)};
  const fui::Rect row = screen.take(fui::LayoutAnchor::Top, TOOL_ROW_HEIGHT);
  const int16_t slotW = static_cast<int16_t>(row.width / reader_toolbar::TOOL_COUNT);
  for (int i = 0; i < reader_toolbar::TOOL_COUNT; ++i) {
    const fui::Rect slot{static_cast<int16_t>(row.x + slotW * i), row.y, slotW, row.height};
    if (i == model_.activeTool) {
      screen.target().stroke(slot.inset(fui::Insets{4, TOOL_PILL_INSET, 4, TOOL_PILL_INSET}),
                             fui::Paint::solid(fui::Color::Black), 2, tokens.controlRadius);
    }
    const fui::Rect iconRect{static_cast<int16_t>(slot.x + (slot.width - 24) / 2),
                             static_cast<int16_t>(slot.y + (slot.height - 24) / 2), 24, 24};
    screen.target().bitmap(iconRect, icons[i], fui::BitmapMode::Center);
    screen.frame().hit(slot, ACTION_TOOL, static_cast<int16_t>(i), fui::InputTouch);
  }
}
