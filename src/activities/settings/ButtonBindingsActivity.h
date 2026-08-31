#pragma once

#include <GfxRenderer.h>

#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

class MappedInputManager;

/**
 * Settings > Controls > Buttons: what each physical key does, per gesture.
 *
 * Two levels. The first lists the keys the board actually has (the two side keys, plus
 * the capacitive Home key where there is one). Picking one opens its six bindings,
 * grouped under "In book" and "Outside book" — the same key can page a book with a
 * single click and open the light panel on the home screen.
 *
 * The action list offered is one list in both groups. Actions that need an open book
 * (bookmark, dictionary, chapter) are drawn greyed under "Outside book" and refuse to
 * be picked there; see util/BoundActionScope.h.
 */
class ButtonBindingsActivity final : public UiListActivity {
 public:
  ButtonBindingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void navigateButtons() override;
  void onBackButton() override;
  ListChrome chrome() const override;
  bool handleCustomInput() override;
  bool drawOverlay() override;

 private:
  /** The key list, or the six bindings of the key picked. */
  enum class View : uint8_t { Buttons, Bindings };

  /** One row of the bindings view: a header, or a binding to edit. */
  struct BindingRow {
    bool isHeader = false;
    bool inBook = false;
    uint8_t gesture = 0;
  };

  void showView(View next);
  /** The keys this board has, in list order; index into it is the row index. */
  void loadButtons();
  void buildBindingRows();
  void openActionPicker(size_t row);
  /** The setting behind a bindings row, or nullptr for a header. */
  uint8_t* bindingFor(size_t row) const;
  const char* buttonLabel(uint8_t button) const;

  View view = View::Buttons;
  std::vector<uint8_t> buttons;
  std::vector<BindingRow> bindingRows;
  /** The key the bindings view is editing; an index into `buttons`. */
  int selectedButton = 0;

  OptionPopup actionPopup;
  /** Values the picker offers, in the order it lists them. */
  std::vector<uint8_t> pickerFunctions;
  /** The row the picker is editing. */
  size_t pickerRow = 0;
  // True while the button press that closed the picker is still held; its release must
  // not fall through to this screen's own Back/Confirm handlers.
  bool popupClosing = false;

  // Row labels own their strings; the ListItems borrow them.
  std::vector<std::string> labels;
  std::vector<std::string> values;
  std::vector<freeink::ui::ListItem> rows;
};
