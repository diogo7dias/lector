#pragma once

#include <GfxRenderer.h>

#include "activities/Activity.h"
#include "components/ListChrome.h"
#include "components/SettingsGrid.h"
#include "components/themes/BaseTheme.h"
#include "components/SliderBand.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

// Base for the two-column settings grids. UiAppHost owns the app-hosting
// protocol; this base layers the grid protocol on top: the selection and scroll
// model over settings_grid, the cell painting (a name over its value, one
// truncation rule), the touch dispatch, the chrome, and the header band an armed
// number takes over.
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
  // One id for the whole armed band: a drag arrives with dragPermille set, a step
  // button with its delta in the event's value.
  static constexpr freeink::ui::ActionId ACTION_SLIDER = 2;
  static constexpr freeink::ui::ActionId ACTION_USER = 3;

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

  // --- helpers ---------------------------------------------------------------
  // The band the grid itself gets: the body minus whatever reservedHeight asked
  // for. Shared by the paint and the layout so the two cannot disagree.
  Rect gridPane() const;
  settings_grid::Shape gridShape() const;
  settings_grid::Layout gridLayout() const;
  int selected() const { return selected_; }
  void setSelected(int index);
  // Up and Down move a whole grid row so the column is kept; Left and Right move
  // one cell.
  void moveSelection(int deltaRows, int deltaCells);
  // Puts the cursor back inside the grid after a rebuild changed its size.
  void clampSelection();

  ButtonNavigator buttonNavigator;
  // Numeric cells arm this instead of stepping once per press. It takes the
  // header's place while it is up, so the grid under it does not move.
  SliderBand valueBand;
  // Arms the band over the header, for `index`'s value.
  void armValueBand(const char* name, int minValue, int maxValue, int smallStep, int largeStep, int current,
                    std::function<void(int)> onChange, std::function<void()> onClose);

 private:
  void buildScreen(UiScreen& screen);
  void buildCell(UiScreen& screen, int index, const settings_grid::Rect& rect);
  void buildRow(UiScreen& screen, int index, const freeink::ui::Rect& box);
  void buildValueBand(UiScreen& screen);
  static void screenTrampoline(UiScreen& screen, void* user);
  static void cellTrampoline(const freeink::ui::ActionEvent& event, void* user);
  static void sliderTrampoline(const freeink::ui::ActionEvent& event, void* user);

  int selected_ = 0;
  int scrollRow_ = 0;
};
