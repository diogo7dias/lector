#pragma once

#include <I18n.h>

#include <cstdint>

#include "components/UiAppHost.h"

enum class LibraryTab : uint8_t { Books = 0, Authors = 1, Series = 2, Count = 3 };

// Reserves a compact segmented switcher bar at the bottom of the Library body
// (so the top rows keep their exact vertical positions) with Books | Authors | Series.
inline void drawLibraryTabBar(UiAppHost::UiScreen& screen, const LibraryTab active,
                              const freeink::ui::ActionId tabAction) {
  namespace fui = freeink::ui;
  const auto& theme = screen.theme();
  const int16_t height = static_cast<int16_t>(theme.rowHeight);
  const fui::Rect band = screen.takeBottom(height, theme.spaceSm);
  const int16_t gap = theme.spaceSm;
  const int16_t w = static_cast<int16_t>((band.width - 2 * gap) / 3);

  static constexpr StrId kLabels[3] = {StrId::STR_TAB_BOOKS, StrId::STR_TAB_AUTHORS, StrId::STR_TAB_SERIES};
  for (int i = 0; i < 3; ++i) {
    fui::ButtonProps props;
    props.label = I18N.get(kLabels[i]);
    props.text = theme.bodyText;
    props.styles = theme.button;
    if (static_cast<int>(active) == i) {
      props.styles.normal = theme.button.active;
    }
    props.action = tabAction;
    props.value = static_cast<int16_t>(i);
    props.minTouchSize = screen.frame().device().minTouchSize;
    props.radius = static_cast<uint8_t>(theme.controlRadius);
    const int16_t x = static_cast<int16_t>(band.x + i * (w + gap));
    const int16_t bw = (i == 2) ? static_cast<int16_t>(band.x + band.width - x) : w;
    screen.button(props, fui::Rect{x, band.y, bw, band.height});
  }
}
