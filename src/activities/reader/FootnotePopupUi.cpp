#include "FootnotePopupUi.h"

#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/icons/readerToolbarIcons.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_DISMISS = 1;
constexpr fui::ActionId ACTION_JUMP = 2;
constexpr fui::ActionId ACTION_PREV = 3;
constexpr fui::ActionId ACTION_NEXT = 4;

constexpr int16_t ACTION_ROW_HEIGHT = 40;
constexpr int16_t PAGER_BTN_SIZE = 36;
constexpr int16_t PAGER_BTN_GAP = 10;
constexpr int16_t JUMP_BTN_PAD_X = 16;
constexpr int16_t SHEET_INNER_SAFETY_PX = 16;
constexpr int MIN_BODY_LINES = 2;
}  // namespace

FootnotePopupUi::FootnotePopupUi(GfxRenderer& renderer) : UiAppHost(renderer) {}

void FootnotePopupUi::begin() {
  resetUi();
  pending_ = Routed{};
  for (fui::ActionId id = ACTION_DISMISS; id <= ACTION_NEXT; ++id) {
    app.on(id, &FootnotePopupUi::onAction, this);
  }
  app.setScreen(&FootnotePopupUi::screenFn, this);
}

FootnotePopupUi::Routed FootnotePopupUi::route(const MappedInputManager& input) {
  pending_ = Routed{};
  const auto touch = routeTouch(input, false, /*routeHeld=*/false);
  pending_.routed = touch.routed;
  return pending_;
}

void FootnotePopupUi::onAction(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<FootnotePopupUi*>(user);
  if (event.action >= ACTION_DISMISS && event.action <= ACTION_NEXT) {
    self->pending_.event = static_cast<Event>(event.action);
  }
  self->app.clearTapFlash();
}

int FootnotePopupUi::bodyWidth(const GfxRenderer& renderer) const {
  const auto& tokens = app.theme();
  return std::max(120, renderer.getScreenWidth() - 2 * tokens.spaceLg - SHEET_INNER_SAFETY_PX);
}

int FootnotePopupUi::maxLinesPerPage(const GfxRenderer& renderer, const bool hasFooter) const {
  const auto& tokens = app.theme();
  const int lineH = std::max(12, renderer.getLineHeight(UI_10_FONT_ID));
  const int headerH = lineH + tokens.spaceSm + 1 + tokens.spaceSm;
  const int footerH = hasFooter ? (tokens.spaceMd + ACTION_ROW_HEIGHT) : 0;
  fui::SheetProps sheetProps;
  const int grabberH = tokens.spaceLg + sheetProps.grabberHeight + tokens.spaceLg + tokens.spaceMd;
  const int chromeH = grabberH + tokens.spaceSm + headerH + footerH + tokens.spaceLg;
  // Keep at least ~38% of the live reading page visible above the card for context and outside-tap dismissal.
  const int maxSheetH = (renderer.getScreenHeight() * 62) / 100;
  const int availH = maxSheetH - chromeH;
  return std::max(MIN_BODY_LINES, availH / lineH);
}

void FootnotePopupUi::screenFn(UiScreen& screen, void* user) {
  static_cast<FootnotePopupUi*>(user)->buildSheet(screen);
}

void FootnotePopupUi::buildSheet(UiScreen& screen) {
  const auto& tokens = screen.theme();
  const int16_t lineH = screen.target().lineHeight(tokens.smallText.font);
  const int visibleLines = std::max(model_.reservedLines, std::max(1, model_.lineCount));
  const bool hasFooter = model_.showPager || model_.showJump;

  const int16_t headerH = static_cast<int16_t>(lineH + tokens.spaceSm + 1 + tokens.spaceSm);
  const int16_t bodyH = static_cast<int16_t>(visibleLines * lineH);
  const int16_t footerH = hasFooter ? static_cast<int16_t>(tokens.spaceMd + ACTION_ROW_HEIGHT) : 0;
  const int16_t contentH = static_cast<int16_t>(tokens.spaceSm + headerH + bodyH + footerH + tokens.spaceLg);

  fui::SheetProps sheetProps;
  sheetProps.anchor = fui::SheetEdge::Bottom;
  sheetProps.dismissAction = ACTION_DISMISS;
  sheetProps.grabberMargin = tokens.spaceLg;
  sheetProps.grabberInset = static_cast<int16_t>(tokens.spaceLg + tokens.spaceMd);
  const int16_t grabberBand =
      static_cast<int16_t>(sheetProps.grabberMargin + sheetProps.grabberHeight + sheetProps.grabberInset);
  screen.sheet(sheetProps, static_cast<int16_t>(contentH + grabberBand));
  screen.insetContent(fui::Insets{0, tokens.spaceLg, 0, tokens.spaceLg});
  screen.spacer(tokens.spaceSm);

  // Header row: "Footnote 1" on left, optional "1/3" page indicator on right.
  {
    const fui::Rect header = screen.takeTop(lineH, tokens.spaceSm);
    fui::TextStyle titleStyle = tokens.smallText;
    titleStyle.bold = true;
    fui::TextStyle infoStyle = tokens.smallText;
    infoStyle.align = fui::TextAlign::Right;
    const int16_t infoW = (model_.pageInfo && model_.pageInfo[0] != '\0')
                              ? screen.target().measureText(infoStyle.font, model_.pageInfo, infoStyle).width
                              : 0;
    const fui::Rect titleRect{header.x, header.y,
                              static_cast<int16_t>(std::max<int>(0, header.width - infoW - tokens.spaceMd)),
                              header.height};
    if (model_.title) screen.target().text(titleRect, model_.title, titleStyle);
    if (model_.pageInfo && model_.pageInfo[0] != '\0') screen.target().text(header, model_.pageInfo, infoStyle);
    const fui::Rect rule = screen.takeTop(1, tokens.spaceSm);
    screen.target().fill(rule, fui::Paint::solid(fui::Color::Black));
  }

  // Wrapped footnote text lines.
  {
    const fui::Rect body = screen.takeTop(bodyH, hasFooter ? tokens.spaceMd : 0);
    fui::TextStyle bodyStyle = tokens.smallText;
    bodyStyle.align = fui::TextAlign::Left;
    const int drawCount = std::min(model_.lineCount, visibleLines);
    for (int i = 0; i < drawCount; ++i) {
      if (!model_.lines || !model_.lines[i]) continue;
      const fui::Rect lineRect{body.x, static_cast<int16_t>(body.y + i * lineH), body.width, lineH};
      screen.target().text(lineRect, model_.lines[i], bodyStyle);
    }
  }

  // Optional footer: < / > card pager on left, "Jump to Note" button on right.
  if (hasFooter) {
    const fui::Rect row = screen.takeTop(ACTION_ROW_HEIGHT, 0);
    const int16_t btnY = static_cast<int16_t>(row.y + (row.height - PAGER_BTN_SIZE) / 2);

    buttonProps_ = fui::ButtonProps{};
    buttonProps_.inputMask = fui::InputTouch;
    buttonProps_.minTouchSize = 0;
    buttonProps_.styles.explicitlySet = true;
    buttonProps_.styles.normal.background = fui::Paint::solid(fui::Color::White);
    buttonProps_.styles.normal.foreground = fui::Paint::solid(fui::Color::Black);
    buttonProps_.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    buttonProps_.styles.normal.borderWidth = 1;
    buttonProps_.styles.normal.radius = tokens.controlRadius;
    buttonProps_.styles.selected = buttonProps_.styles.normal;
    buttonProps_.styles.focused = buttonProps_.styles.normal;
    buttonProps_.styles.disabled = buttonProps_.styles.normal;
    buttonProps_.styles.disabled.border = fui::Paint::none();
    buttonProps_.styles.active = buttonProps_.styles.normal;
    buttonProps_.styles.active.background = fui::Paint::solid(fui::Color::Black);
    buttonProps_.styles.active.foreground = fui::Paint::solid(fui::Color::White);

    int16_t leftUsed = 0;
    if (model_.showPager) {
      buttonProps_.label = nullptr;
      if (model_.hasPrev) {
        buttonProps_.icon = fui::bitmapFromIcon(icon_reader_back_24);
        buttonProps_.action = ACTION_PREV;
        screen.button(buttonProps_, fui::Rect{row.x, btnY, PAGER_BTN_SIZE, PAGER_BTN_SIZE});
      }
      if (model_.hasNext) {
        buttonProps_.icon = fui::bitmapFromIcon(icon_reader_next_24);
        buttonProps_.action = ACTION_NEXT;
        screen.button(buttonProps_, fui::Rect{static_cast<int16_t>(row.x + PAGER_BTN_SIZE + PAGER_BTN_GAP), btnY,
                                              PAGER_BTN_SIZE, PAGER_BTN_SIZE});
      }
      leftUsed = static_cast<int16_t>(2 * PAGER_BTN_SIZE + 2 * PAGER_BTN_GAP);
    }

    if (model_.showJump && model_.jumpLabel) {
      fui::TextStyle labelStyle = tokens.smallText;
      labelStyle.bold = true;
      labelStyle.align = fui::TextAlign::Center;
      const int16_t textW = screen.target().measureText(labelStyle.font, model_.jumpLabel, labelStyle).width;
      const int16_t maxW = static_cast<int16_t>(std::max<int>(64, row.width - leftUsed));
      const int16_t btnW = std::min<int16_t>(maxW, static_cast<int16_t>(textW + 2 * JUMP_BTN_PAD_X));
      buttonProps_.icon = fui::BitmapRef{};
      buttonProps_.label = model_.jumpLabel;
      buttonProps_.text = labelStyle;
      buttonProps_.action = ACTION_JUMP;
      screen.button(buttonProps_, fui::Rect{static_cast<int16_t>(row.right() - btnW), btnY, btnW, PAGER_BTN_SIZE});
    }
  }
}
