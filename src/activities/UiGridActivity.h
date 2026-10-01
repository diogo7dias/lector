#pragma once

#include <GfxRenderer.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/ListChrome.h"
#include "components/SettingsGrid.h"
#include "components/UiAppHost.h"
#include "components/WrappedListWindow.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

// Base for the settings screens: one column of name-and-value rows in the contents look,
// under numbered headings. UiAppHost owns the app-hosting protocol; this base layers the
// selection and scroll model, the touch dispatch, and the chrome on top. A numeric cell opens the
// shared slider dialog (IntervalSelectionActivity) rather than editing in place.
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
  // A numbered heading drawn above this cell (nullptr for none).
  virtual const char* cellHeading(int index) const { return nullptr; }
  // chrome() as it is painted.
  ListChrome shownChrome() const;
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

  // --- helpers ---------------------------------------------------------------
  // The band the grid itself gets: the body minus whatever reservedHeight asked
  // for. Shared by the paint and the layout so the two cannot disagree.
  Rect gridPane() const;
  // One column of contents rows on every board, a window over their heights
  // (WrappedListWindow): rowHeightFor measures one row; keysOnlyWindow says which rows
  // the pane shows from scrollRow_ with the selection kept visible.
  int rowHeightFor(int index) const;
  wrapped_list::Window keysOnlyWindow() const;
  int selected() const { return selected_; }
  void setSelected(int index);
  // Up and Down move by rows; Left and Right by one cell, which in one column is a row.
  void moveSelection(int deltaRows, int deltaCells);
  // Puts the cursor back inside the grid after a rebuild changed its size.
  void clampSelection();

  ButtonNavigator buttonNavigator{ButtonNavigator::LIST_REPEAT_INTERVAL_MS, ButtonNavigator::LIST_REPEAT_START_MS};

 private:
  void buildScreen(UiScreen& screen);
  static void screenTrampoline(UiScreen& screen, void* user);
  static void cellTrampoline(const freeink::ui::ActionEvent& event, void* user);
  // The contents look's rows, headings included, rebuilt each build; labels borrow
  // the subclass's strings for the length of the build.
  void buildContents(UiScreen& screen, const Rect& pane, const wrapped_list::Window& win);
  std::vector<freeink::ui::ListItem> contentsItems_;
  std::vector<std::string> contentsText_;
  int selected_ = 0;
  int scrollRow_ = 0;
  // What the last build laid out, for the chevrons render() paints after the app.
  Rect scrollArrowBand_{};
  list_scrollbar::Arrows scrollArrows_{false, false};
};
