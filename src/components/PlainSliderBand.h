#pragma once

#include <FreeInkUICore.h>
#include <components/controls/progress-bar.h>

#include <algorithm>

// The keys-only slider: a caption line (name left, readout right) over a plain
// bar, optionally on a filled band.
//
// Only the X4 Pro has a touch panel, and the FreeInkUI slider row is built for
// it: a capsule to drag and two step buttons to tap. On the X3 and the X4 there
// is nothing to aim at, so those controls would be decoration over a value the
// buttons were driving anyway. Those boards keep the bar they always had, and
// both slider surfaces (the status screens and the settings grid's armed band)
// pass through here so the two cannot drift apart.
namespace plain_slider_band {

// `Screen` is FreeInkApp's screen type; a template so this header does not need
// the host's nested alias.
template <class Screen>
void draw(Screen& screen, const freeink::ui::Rect& rect, const char* label, const char* value, const int sliderValue,
          const int sliderMax, const bool inverted) {
  namespace fui = freeink::ui;
  const auto& theme = screen.theme();
  auto& target = screen.frame().target();

  if (inverted) target.fill(rect, fui::Paint::solid(fui::Color::Black));

  const int16_t lineHeight = target.lineHeight(theme.smallText.font);
  const int16_t inset = theme.spaceSm;
  const fui::Rect line{static_cast<int16_t>(rect.x + inset), static_cast<int16_t>(rect.y + inset),
                       static_cast<int16_t>(std::max(0, rect.width - inset * 2)), lineHeight};

  fui::TextStyle caption = theme.smallText;
  caption.inverted = inverted;
  if (label != nullptr && label[0] != '\0') {
    caption.align = fui::TextAlign::Left;
    target.text(line, label, caption);
  }
  if (value != nullptr && value[0] != '\0') {
    caption.align = fui::TextAlign::Right;
    target.text(line, value, caption);
  }

  constexpr int16_t kBarHeight = 6;
  const int16_t barTop =
      static_cast<int16_t>(std::min<int>(line.y + lineHeight + theme.spaceSm, rect.y + rect.height - kBarHeight));
  if (barTop < rect.y) return;

  fui::ProgressBarProps bar;
  bar.value = sliderValue;
  bar.max = sliderMax > 0 ? sliderMax : 1;
  bar.fill = fui::Paint::solid(inverted ? fui::Color::White : fui::Color::Black);
  bar.border = bar.fill;
  bar.borderWidth = 1;
  fui::progressBar(screen.frame(), fui::Rect{line.x, barTop, line.width, kBarHeight}, bar);
}

}  // namespace plain_slider_band
