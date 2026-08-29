#pragma once

#include <SdCardFontRegistry.h>

#include <cstdint>
#include <string>
#include <vector>

#include "TextSettingsPreview.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "components/SettingsGrid.h"
#include "components/SliderBand.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"
#include "util/HoldRepeat.h"

// Reader text settings: a live page preview over ONE scrolling list of every setting,
// banded into sections (Type / Spacing / Margins / Reading aids). The tab bar this screen
// used to carry is gone — the sections are drawList's own heading rows, which navigation
// steps past, so Up/Down walks the whole tree without a mode switch.
//
// Numeric rows are edited IN PLACE (Confirm arms the row, Up/Down move the value) rather
// than through a popup, because a popup would cover the very preview the number is being
// judged against.
// Auto-repeat timing for an armed numeric row. Deliberately faster than ButtonNavigator's
// list defaults: these ranges run to 150, so a list-speed hold would never finish.

class TextSettingsActivity final : public Activity {
 public:
  TextSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const SdCardFontRegistry* registry);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Every cell the screen can show, in grid order. Cells are laid out two to a row, so
  // the order below is also the pairing: Font sits beside Size, Line Spacing beside Extra
  // Spacing, and so on. Three cells come and go and the grid reflows around them.
  enum class Row : uint8_t {
    Font,
    Size,
    PaperbackLook,
    LineSpacing,
    ExtraSpacing,
    ParagraphSpacing,
    Alignment,
    IndentMode,
    IndentPercent,     // only in Custom % mode
    HorizontalMargin,  // labelled "Margin" in All Sides, where it stands for every side
    MarginLink,
    VerticalMargin,  // shown while top and bottom are linked
    TopMargin,       // replaces VerticalMargin when they are not
    BottomMargin,
    DynamicMargins,
    FocusReading,
    GuideDots,
    HiddenDots,  // sub-option of GuideDots: only listed while Guide Dots is on
    Hyphenation,
    EmbeddedTextStyle,
    EmbeddedLayoutStyle,
    AntiAliasing,
    DebugBorders,
  };

  // What Confirm does on a cell, and therefore what the button hint says.
  enum class RowKind : uint8_t { Toggle, Picker, Number, FontList };

  // The screen is either walking the list or showing the font picker full screen. The
  // picker is a mode rather than its own activity so it can hand focus straight back to
  // the Font row without a result round-trip.
  enum class Mode : uint8_t { List, FontPicker };

  static RowKind kindOf(Row row);
  // The rows that apply right now, in draw order. Rebuilt from the live settings because
  // three rows come and go (indent %, the linked/split vertical margins, hidden dots).
  std::vector<Row> visibleRows() const;
  StrId rowNameId(Row row) const;
  std::string rowValueText(Row row) const;
  // Numeric rows share one editing path; these give it the field and its range.
  uint8_t* numberField(Row row) const;
  void numberRange(Row row, int& minValue, int& maxValue) const;
  void setEditedValue(int value);
  void applyNumber(Row row, int value);

  void activateRow(Row row);
  void leaveEdit();

  void applyFamily(int listIndex);
  void applySize(int listIndex);
  // Repopulates sizes_ (and currentSizeIndex_) from the active family's installed point
  // sizes. Call after any family change.
  void rebuildSizeList();
  void openSizePicker();

  // Vertical layout of the preview and grid panes. Shared by render() (to draw) and
  // loop() (to hit-test touch) so the two cannot drift.
  struct PaneGeometry {
    int previewTop;
    int previewHeight;
    int listTop;
    int listHeight;
  };
  PaneGeometry paneGeometry() const;

  // The grid the last render() drew, so loop() hit-tests exactly what is on screen.
  settings_grid::Layout gridLayout() const;
  void drawCell(const settings_grid::Rect& rect, Row row, bool selected);

  // Up and Down move a whole grid row and keep the column; Left and Right move one cell.
  // While a numeric cell is armed, Up and Down move its value instead.
  void moveSelection(int deltaRows, int deltaCells);

  struct FontEntry {
    std::string name;
    bool isBuiltin;
    uint8_t settingIndex;
  };

  struct SizeEntry {
    std::string name;  // the point size, rendered for display ("14 pt")
    uint8_t pointSize;
  };

  const SdCardFontRegistry* registry_;
  ButtonNavigator buttonNavigator_;
  OptionPopup optionPopup_;
  // The armed number's control: it takes the header's place, so the preview under it
  // keeps showing what the value does while the value moves.
  SliderBand valueBand_;
  std::vector<FontEntry> fonts_;
  std::vector<SizeEntry> sizes_;
  textsettings::PreviewLayout previewLayout_;  // cached preview line layout; relaid only on setting/geometry change

  Mode mode_ = Mode::List;
  int selectedIndex_ = 0;  // index into visibleRows()
  int scrollRow_ = 0;      // first grid row drawn
  int fontPickerIndex_ = 0;
  int fontPickerScroll_ = 0;

  // Set while a numeric row is armed. The preview redraw is debounced so holding Up does
  // not queue one full e-ink pass per step.
  bool editing_ = false;
  uint32_t pendingRedrawAt_ = 0;
  // An edited number is written once the value stops moving, not on every button press.
  // See commitSettings() in the .cpp for why.
  bool settingsDirty_ = false;
  uint32_t pendingSaveAt_ = 0;
  void commitSettings();

  int currentFamilyIndex_ = 0;
  int currentSizeIndex_ = 0;

  ThemeMetrics metrics_ = {};
  int afterHeader = 0;
  int bottomReserved = 0;
  int usableHeight = 0;
};
