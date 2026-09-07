#pragma once
#include <BoardConfig.h>
#include <FreeInkUIGfxRenderer.h>
#include <HalGPIO.h>

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
  tokens.bodyText.bold = metrics.listTitleBold;

  // The keys-only boards (X3, X4) keep the look they had before the FreeInkUI
  // migration, which is not the SDK's default. Split at runtime on hasTouch(),
  // not on a device macro: the `default` environment builds one C3 binary for
  // both keys-only boards, so a compile-time split could not tell them from the
  // X4 Pro anyway (same reasoning as test/device_look).
  if (!gpio.hasTouch()) {
    // Regular everywhere in menus. The UI families ship a regular face only and
    // fill their bold slot with it (see main.cpp), so a bold title was never a
    // heavier cut — just the same glyphs asking for a face that does not exist.
    // Weight hierarchy in this UI comes from size and from the inverted band.
    tokens.titleText.bold = false;
    tokens.smallText.bold = false;
    tokens.bodyText.bold = false;
    // Header band: solid black with the title knocked out white, matching
    // BaseTheme::drawHeader, which every chrome-drawn header already uses. Only
    // the fui-drawn headers (the OPDS browser) were coming out white-on-white
    // paper with a rule under them.
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
  }
  return tokens;
}

// Same face, same size, same weight for a setting's NAME and its VALUE on the
// keys-only boards. Screen::list() otherwise themes the label from bodyText and
// the value from smallText, which is a size apart: "Font size / 14" came out
// with the number visibly smaller than the name beside it. A touch board keeps
// the SDK pairing, where the smaller value is a deliberate hierarchy.
//
// Call after filling a ListProps and before screen.list(). Leaves an explicitly
// styled value alone, so a screen that means its value to differ still can.
inline void applyKeysOnlyValueStyle(freeink::ui::ListProps& props, const freeink::ui::ThemeTokens& tokens) {
  if (gpio.hasTouch()) return;
  if (!freeink::ui::textStyleUnset(props.valueText)) return;
  props.valueText = tokens.bodyText;
}
