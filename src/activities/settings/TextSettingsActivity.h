#pragma once

#include <SdCardFontRegistry.h>

#include <cstdint>
#include <string>
#include <vector>

#include "TextSettingsPreview.h"
#include "activities/UiGridActivity.h"
#include "components/OptionPopup.h"
#include "components/themes/BaseTheme.h"
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

class TextSettingsActivity final : public UiGridActivity {
 public:
  TextSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const SdCardFontRegistry* registry);

  void onEnter() override;
  void onExit() override;

 protected:
  int cellCount() const override;
  const char* cellName(int index) const override;
  const char* cellValue(int index) const override;
  void activateCell(int index) override;
  ListChrome chrome() const override;
  bool handleCustomInput() override;
  void onBackButton() override;
  bool drawOverlay() override;
  int reservedHeight() const override;
  void drawReserved(const Rect& rect) override;

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
  OptionPopup optionPopup_;
  std::vector<FontEntry> fonts_;
  std::vector<SizeEntry> sizes_;
  textsettings::PreviewLayout previewLayout_;  // cached preview line layout; relaid only on setting/geometry change

  // The two strings a cell is drawn from, rebuilt on demand: the base asks for
  // them one cell at a time and draws each immediately.
  mutable std::string cellNameScratch_;
  mutable std::string cellValueScratch_;

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
