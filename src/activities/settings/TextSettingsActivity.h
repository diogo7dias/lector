#pragma once

#include <SdCardFontRegistry.h>

#include <cstdint>
#include <string>
#include <vector>

#include "TextSettingsPreview.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "components/ValueBarPopup.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

// Reader text settings with a shared live preview pane: tab bar
// (Font | Size | Layout | Style) is position 0 of the Up/Down nav ring, same
// idiom as SettingsActivity. Family/Size rows apply on Confirm; Layout/Style
// rows toggle or open an OptionPopup picker. (Tab::Family/Style are the enum
// names for the Font/Style tabs.)
class TextSettingsActivity final : public Activity {
 public:
  enum class Tab : uint8_t { Family, Size, Layout, Style, Count };

  TextSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const SdCardFontRegistry* registry,
                       Tab initialTab = Tab::Family);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Row indices per tab. enum class (not plain enum) so a LayoutRow can't be
  // silently confused with a StyleRow of equal value. The Layout tab does NOT map
  // 1:1 to the drawn list — visibleLayoutRows() hides rows that don't apply, so
  // the drawn index is resolved through that list, never cast straight to LayoutRow.
  enum class LayoutRow {
    LineSpacing,
    ParaSpacing,     // extraParagraphSpacing toggle (half-line block gap)
    ParaSpacingPct,  // paragraphSpacing 0..150 % bar (stacks on top of ParaSpacing)
    Alignment,
    UniformMargins,      // toggle: all sides use screenMargin vs independent top/bottom
    ScreenMargin,        // horizontal margin (also vertical when uniform)
    ScreenMarginTop,     // only shown when uniform is off
    ScreenMarginBottom,  // only shown when uniform is off
    DynamicMargins,
    IndentMode,     // Book vs Custom %
    IndentPercent,  // only shown in Custom % mode
    DebugBorders,
    Count
  };
  enum class StyleRow { FocusReading, Hyphenation, EmbeddedStyle, AntiAliasing, Count };

  void applyFamily(int listIndex);
  void applySize(int listIndex);
  // The Layout tab hides rows that don't apply (top/bottom margins only when
  // uniform is off; indent % only in Custom% mode), so the visible list is built
  // from the live settings instead of being a fixed 1:1 with the enum.
  std::vector<LayoutRow> visibleLayoutRows() const;
  std::string layoutRowName(LayoutRow row) const;
  bool isLayoutToggleRow(LayoutRow row) const;
  void confirmLayoutRow(LayoutRow row);
  void confirmStyleRow(int row);
  // Applies the row at the given list index for the active tab (Confirm and tap share this).
  void activateRow(int row);

  // Vertical layout of the preview/tab-bar/list panes.
  // Shared by render() (to draw) and loop() (to hit-test touch) to avoid drift
  struct PaneGeometry {
    int previewTop;
    int tabTop;
    int listTop;
    int listHeight;
  };
  PaneGeometry paneGeometry() const;
  std::string layoutValueText(LayoutRow row) const;
  std::string styleValueText(int row) const;
  // True when the focused list row is a setting the preview cannot reflect.
  bool focusedRowHasNoPreview() const;
  void switchTab(int direction = 1);
  int currentListSize() const;
  // Navigation ring position for the active tab: 0 = tab bar, 1..N = list item N-1.
  int& selectedIndex();
  int selectedIndex() const;

  struct FontEntry {
    std::string name;
    bool isBuiltin;
    uint8_t settingIndex;
  };

  struct SizeEntry {
    std::string name;
    uint8_t settingIndex;
  };

  const SdCardFontRegistry* registry_;
  ButtonNavigator buttonNavigator_;
  OptionPopup optionPopup_;
  ValueBarPopup valueBar_;
  std::vector<FontEntry> fonts_;
  std::vector<SizeEntry> sizes_;
  textsettings::PreviewLayout previewLayout_;  // cached preview line layout; relaid only on setting/geometry change

  Tab tab_;
  int selectedIndex_[static_cast<int>(Tab::Count)] =
      {};  // per-Tab nav position (0 = tab bar, 1..N = row); set in onEnter
  int currentFamilyIndex_ = 0;
  int currentSizeIndex_ = 0;

  ThemeMetrics metrics_ = {};
  int afterHeader = 0;
  int bottomReserved = 0;
  int usableHeight = 0;
  int previewHeight = 0;
};
