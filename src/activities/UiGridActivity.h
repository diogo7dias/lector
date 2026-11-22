#pragma once

#include <GfxRenderer.h>

#include "activities/Activity.h"
#include "components/ListChrome.h"
#include "components/SettingsGrid.h"
#include "components/UiAppHost.h"
#include "components/WrappedListWindow.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

// Base for settings rows (X4 Pro/keys-only) and grids (other touch boards). UiAppHost owns the app-hosting
// protocol; this base layers the grid protocol on top: the selection and scroll
// model over settings_grid, the cell painting (a name over its value, one
// truncation rule, plus heading and card looks), the touch dispatch, and the chrome.
// What a numeric cell does is the screen's business.
//
// Both grid screens used to do all of that themselves, including a hand-rolled
// hit test that walked the same cells the paint had just walked. Anything that
// is a grid of name-over-value cells belongs here; a screen that is a list does
// not, and uses UiListActivity.
class UiGridActivity : public Activity, protected UiAppHost {
 public:
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  void onRevealed() override { cleanPaint_ = true; }

 protected:
  // Base-owned actions; subclass-registered ones start at ACTION_USER.
  static constexpr freeink::ui::ActionId ACTION_CELL = 1;
  static constexpr freeink::ui::ActionId ACTION_USER = 2;

  UiGridActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput);

  // --- subclass contract -----------------------------------------------------
  virtual int cellCount() const = 0;
  // The two lines of a cell. Both may be nullptr; the strings must outlive the
  // render, so they come from the subclass's own storage.
  virtual const char* cellName(int index) const = 0;
  virtual const char* cellValue(int index) const = 0;
  // Confirm on the selection, or a tap on a cell.
  virtual void activateCell(int index) = 0;
  // What the base paints around the grid. Default: the title from headerTitle().
  virtual ListChrome chrome() const;
  virtual const char* headerTitle() const { return nullptr; }
  // First hook in loop(); return true when the pass is consumed.
  virtual bool handleCustomInput() { return false; }
  virtual void onBackButton() { finish(); }
  // Drawn over the finished page. Return true when the overlay pushed its own
  // refresh (GUI.drawPopup does).
  virtual bool drawOverlay() { return false; }
  // A band above the grid that the subclass paints itself, for content the grid
  // cannot express: the live text preview. Height in pixels, 0 for none.
  virtual int reservedHeight() const { return 0; }
  virtual void drawReserved(const Rect& rect) {}
  // How a cell is drawn in the one-column rows. Header: a section heading in the inverted
  // band that hugs its label; never selected, tapped or landed on by a key. Card: a framed
  // box with the name over its value, for a hub whose value is a summary line.
  enum class CellLook : uint8_t { Row, Header, Card };
  virtual CellLook cellLook(int index) const { return CellLook::Row; }
  bool isHeaderCell(int index) const { return cellLook(index) == CellLook::Header; }

  // --- helpers ---------------------------------------------------------------
  // The band the grid itself gets: the body minus whatever reservedHeight asked
  // for. Shared by the paint and the layout so the two cannot disagree.
  Rect gridPane() const;
  settings_grid::Shape gridShape() const;
  settings_grid::Layout gridLayout() const;
  // X4 Pro and keys-only boards: one column of rows as tall as their wrapped text, so the
  // layout is a window over variable heights (WrappedListWindow) rather than a
  // grid of equal cells. rowHeightFor measures one row; keysOnlyWindow says
  // which rows the pane shows from scrollRow_ with the selection kept visible.
  bool usesWrappedRows() const;
  int rowHeightFor(int index) const;
  wrapped_list::Window keysOnlyWindow() const;
  // One screen of rows on (1) or back (-1): the touch band's page pair.
  void pageBy(int direction);
  int selected() const { return selected_; }
  void setSelected(int index);
  // Up and Down move a whole grid row so the column is kept; Left and Right move
  // one cell.
  void moveSelection(int deltaRows, int deltaCells);

  ButtonNavigator buttonNavigator{ButtonNavigator::LIST_REPEAT_INTERVAL_MS, ButtonNavigator::LIST_REPEAT_START_MS};

 private:
  // See UiListActivity::cleanPaint_.
  bool cleanPaint_ = true;
  void buildScreen(UiScreen& screen);
  void buildCell(UiScreen& screen, int index, const settings_grid::Rect& rect);
  void buildRow(UiScreen& screen, int index, const freeink::ui::Rect& box);
  // UiGridCellLook.cpp: the heading band and the framed card.
  void buildHeader(UiScreen& screen, int index, const freeink::ui::Rect& box);
  void buildCard(UiScreen& screen, int index, const freeink::ui::Rect& box);
  int cardHeightFor(int index) const;
  // The nearest landable cell from `index` walking `direction` (+1 or -1), or -1.
  int landableFrom(int index, int direction) const;
  // The two-column touch grid's cells share one height: the tallest any cell
  // needs for its wrapped name over its wrapped value, so none is cut.
  int tallestCellHeight() const;
  static void screenTrampoline(UiScreen& screen, void* user);
  static void cellTrampoline(const freeink::ui::ActionEvent& event, void* user);
  int selected_ = 0;
  int scrollRow_ = 0;
  // What the last build laid out, for the chevrons render() paints after the app.
  Rect scrollArrowBand_{};
  list_scrollbar::Arrows scrollArrows_{false, false};
  // Which screen of rows the last build showed, for the counter beside the battery.
  list_scrollbar::Page scrollPage_{0, 0};
  char pageText_[12] = {};
};
