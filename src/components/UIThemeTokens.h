#pragma once
#include <BoardConfig.h>
#include <FreeInkUIGfxRenderer.h>

#include "UITheme.h"
#include "UiRowHeight.h"

// Merges the active UITheme's shape (row gaps, radii, insets, selection
// style) with the uiScale-derived sizes into FreeInkUI theme tokens: the
// theme says what lists look like, the scale says how big they are.
// Everything read here is plain data from ThemeMetrics — the same values an
// SD-card theme file will eventually supply.
inline freeink::ui::ThemeTokens uiThemeTokens(const freeink::ui::GfxRendererTarget& target) {
  namespace fui = freeink::ui;
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();

  const int16_t bodyLineHeight = target.lineHeight(fui::GfxRendererTarget::FONT_BODY);
  fui::ThemeTokens tokens = fui::themeTokensForLineHeight(bodyLineHeight);
  // The SDK sizes its row for a label plus a subtitle; Lector's lists are
  // single-line almost everywhere, so the row is taken down to a share of it.
  tokens.rowHeight = static_cast<int16_t>(ui_row_height::scaled(tokens.rowHeight, bodyLineHeight));
  tokens.listRowGap = static_cast<int16_t>(metrics.listRowGap);
  tokens.listRowRadius = static_cast<uint8_t>(metrics.listRowRadius);
  tokens.listInset = static_cast<int16_t>(metrics.listInset);
  tokens.listSidePadding = static_cast<int16_t>(metrics.listSidePadding);
  tokens.listSelectionStyle = static_cast<fui::SelectionStyle>(metrics.listSelectionStyle);
  tokens.listScrollWidth = static_cast<int16_t>(metrics.listScrollWidth);
  tokens.listScrollSide = static_cast<uint8_t>(metrics.listScrollSide);
  // The scroll track hugs the band edge; on boards whose panel sits recessed
  // behind the bezel the edge columns are covered, so push the indicator
  // inward past the covered side. Bezel truth is per-board data
  // (BoardConfig::ViewableInsets); lists render in the portrait UI frame, so
  // the panel-native portrait insets apply directly.
  const auto& vi = BoardConfig::ACTIVE.viewableInsets;
  tokens.listScrollInset = static_cast<int16_t>(metrics.listScrollSide == 1 ? vi.left : vi.right);
  // Screen::header()/status() band height. Without this the SDK's
  // line-height-derived default applies and fui-drawn headers (OPDS) come out
  // a different height than every GUI.drawHeader band.
  tokens.headerHeight = static_cast<int16_t>(metrics.headerHeight);
  tokens.headerSidePadding = static_cast<int16_t>(metrics.headerSidePadding);
  tokens.headerUnderline = static_cast<uint8_t>(metrics.headerUnderlineSize);
  tokens.headerTitleAlign = static_cast<fui::TextAlign>(metrics.headerTitleAlign);
  // Control-panel shape (sheet corners, tiles, step buttons, capsule slider),
  // so the control center follows the theme like every list and header does.
  tokens.controlRadius = static_cast<uint8_t>(metrics.controlRadius);
  tokens.sheetRadius = static_cast<uint8_t>(metrics.sheetRadius);
  tokens.capsuleRadius = static_cast<uint8_t>(metrics.capsuleRadius);
  // One look on every board, touch or keys: regular weight, one size, a black
  // header band with the title knocked out white, the selected row the same
  // filled band. Not split by board: the X4 Pro was left with the SDK's own
  // look once before and the user wants one firmware look, so nothing here asks
  // what the board is.
  //
  // Regular everywhere. The UI families ship a regular face only and fill their
  // bold slot with it (see main.cpp), so a bold title was never a heavier cut,
  // just the same glyphs asking for a face that does not exist. What sets a
  // heading apart is the inverted band, never the weight.
  tokens.titleText.bold = false;
  tokens.smallText.bold = false;
  tokens.bodyText.bold = false;
  // Header band: solid black with the title knocked out white, matching
  // BaseTheme::drawHeader, which every chrome-drawn header already uses.
  tokens.popup.explicitlySet = true;
  tokens.popup.normal.background = fui::Paint::solid(fui::Color::Black);
  tokens.popup.normal.foreground = fui::Paint::solid(fui::Color::White);
  tokens.popup.normal.border = fui::Paint::none();
  tokens.popup.normal.borderWidth = 0;
  tokens.popup.selected = tokens.popup.normal;
  tokens.popup.focused = tokens.popup.normal;
  tokens.popup.active = tokens.popup.normal;
  tokens.popup.disabled = tokens.popup.normal;
  tokens.titleText.color = fui::Color::White;
  // The rule under the band belongs to a white header; a black band is its own
  // separator.
  tokens.headerUnderline = 0;
  // Selected row: the same filled band, text knocked out white. This is what
  // metrics.listSelectionStyle == 0 (InvertFill) already asks for, so it is
  // stated here only so a theme cannot leave the rows outlined instead.
  tokens.listSelectionStyle = fui::SelectionStyle::InvertFill;
  // No scroll track. Lists say "more" with the two chevrons UiListActivity draws
  // outside the row band (ListScrollbar.h), so the SDK draws no indicator and
  // the rows keep the width the track used to take.
  tokens.listScrollWidth = 0;
  return tokens;
}

// Same face, same size, same weight for a setting's NAME and its VALUE.
// Screen::list() otherwise themes the label from bodyText and the value from
// smallText; the two slots now carry the same font, but a theme file could
// split them again, so the pairing is stated here.
//
// Call after filling a ListProps and before screen.list(). Leaves an explicitly
// styled value alone, so a screen that means its value to differ still can.
inline void applyKeysOnlyValueStyle(freeink::ui::ListProps& props, const freeink::ui::ThemeTokens& tokens) {
  if (!freeink::ui::textStyleUnset(props.valueText)) return;
  props.valueText = tokens.bodyText;
}

// Labels and subtitles wrap, never truncate: a row grows by the lines its text
// needs (list() sizes wrapped rows per row). The caps are generous enough that
// the last-line ellipsis the SDK applies past them is never reached by a
// setting name or a chapter title on this panel. Values stay single-line: the
// SDK lays the value out beside the label and wraps the label around it.
constexpr uint8_t kListLabelMaxLines = 4;
constexpr uint8_t kListSubtitleMaxLines = 3;
inline void applyWrappingRowStyle(freeink::ui::ListProps& props, const freeink::ui::ThemeTokens& tokens) {
  if (freeink::ui::textStyleUnset(props.labelText)) {
    props.labelText = tokens.bodyText;
    props.labelText.maxLines = kListLabelMaxLines;
  }
  if (freeink::ui::textStyleUnset(props.subtitleText)) {
    props.subtitleText = tokens.smallText;
    props.subtitleText.maxLines = kListSubtitleMaxLines;
  }
}

// Section headings: a full-width inverted band, label centred in white, matching
// BaseTheme::drawList's rowIsHeader path. The SDK default is a left-aligned
// underlined caption — that is what the in-book menu was showing. One place,
// every UiListActivity, so Controls and the reader menu cannot drift.
inline void applyInvertedSectionHeaderStyle(freeink::ui::ListProps& props, const freeink::ui::ThemeTokens& tokens) {
  if (!freeink::ui::textStyleUnset(props.headerText)) return;
  props.headerText = tokens.bodyText;
  props.headerText.align = freeink::ui::TextAlign::Center;
  props.headerText.color = freeink::ui::Color::White;
  props.headerUnderline = false;
  props.sectionGap = 0;
  if (props.headerRowHeight == 0) {
    props.headerRowHeight = props.rowHeight > 0 ? props.rowHeight : tokens.rowHeight;
  }
}
