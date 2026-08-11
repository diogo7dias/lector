#pragma once
#include <I18n.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

class OptionPopup {
 public:
  void show(StrId titleId, const StrId* optionIds, int optionCount, int currentIndex,
            std::function<void(int)> onSelect) {
    title = I18N.get(titleId);
    ownedStrings.resize(optionCount);
    for (int i = 0; i < optionCount; i++) {
      ownedStrings[i] = I18N.get(optionIds[i]);
    }
    selectedIndex = currentIndex;
    onSelectCallback = std::move(onSelect);
    layoutValid = false;
    active = true;
  }

  void show(const char* titleStr, const char* const* options, int optionCount, int currentIndex,
            std::function<void(int)> onSelect) {
    title = titleStr;
    ownedStrings.resize(optionCount);
    for (int i = 0; i < optionCount; i++) {
      ownedStrings[i] = options[i];
    }
    selectedIndex = currentIndex;
    onSelectCallback = std::move(onSelect);
    layoutValid = false;
    active = true;
  }

  void show(StrId titleId, const std::vector<std::string>& options, int currentIndex,
            std::function<void(int)> onSelect) {
    title = I18N.get(titleId);
    ownedStrings = options;
    disabled.clear();
    leftAligned = false;
    selectedIndex = currentIndex;
    onSelectCallback = std::move(onSelect);
    layoutValid = false;
    active = true;
  }

  // Variant for lists whose rows are not all usable right now (the reader's Quick Menu).
  // Disabled rows stay visible and keep their place — a row that vanished when a page had
  // no footnote would move every row under it and break the muscle memory the pop-up
  // exists to reward — but the cursor steps over them and Confirm cannot land on one.
  //
  // leftAlign lines up the rows so the disabled marker occupies a fixed column instead of
  // shunting the label sideways on each row that carries it.
  void showWithDisabled(StrId titleId, const std::vector<std::string>& options, const std::vector<bool>& disabledRows,
                        int currentIndex, bool leftAlign, std::function<void(int)> onSelect) {
    show(titleId, options, currentIndex, std::move(onSelect));
    disabled = disabledRows;
    leftAligned = leftAlign;
    // Never open on a row Confirm would refuse.
    if (isDisabled(selectedIndex)) selectedIndex = nextEnabled(selectedIndex, 1);
  }

  bool handleInput(MappedInputManager& input, const std::function<void()>& requestUpdate) {
    if (!active) return false;

    int tx = 0;
    int ty = 0;
    if (input.wasScreenTouchDown(tx, ty)) {
      const auto& hitLayout = getLayout(input.getRenderer());
      for (int i = 0; i < static_cast<int>(hitLayout.options.size()); i++) {
        if (contains(hitLayout.options[i], tx, ty)) {
          if (selectedIndex != i && !isDisabled(i)) {
            selectedIndex = i;
            requestUpdate();
          }
          break;
        }
      }
      return true;
    }
    if (input.wasScreenTapped(tx, ty)) {
      const auto& hitLayout = getLayout(input.getRenderer());
      for (int i = 0; i < static_cast<int>(hitLayout.options.size()); i++) {
        if (contains(hitLayout.options[i], tx, ty)) {
          // A tap on a disabled row is neither a choice nor a dismissal: it lands
          // inside the dialog, so the pop-up simply stays open.
          if (isDisabled(i)) return true;
          selectedIndex = i;
          active = false;
          if (onSelectCallback) onSelectCallback(selectedIndex);
          requestUpdate();
          return true;
        }
      }
      // Taps on the dialog chrome (title, padding) keep the popup open; taps outside dismiss it
      if (contains(hitLayout.dialog, tx, ty)) return true;
      active = false;
      requestUpdate();
      return true;
    }

    if (input.wasPressed(MappedInputManager::Button::NavPrevious)) {
      selectedIndex = nextEnabled(selectedIndex, -1);
      requestUpdate();
      return true;
    } else if (input.wasPressed(MappedInputManager::Button::NavNext)) {
      selectedIndex = nextEnabled(selectedIndex, 1);
      requestUpdate();
      return true;
    } else if (input.wasPressed(MappedInputManager::Button::Confirm)) {
      // Every row disabled: there is nothing to choose, so Confirm dismisses rather
      // than firing an action the caller declared impossible.
      if (isDisabled(selectedIndex)) {
        active = false;
        requestUpdate();
        return true;
      }
      active = false;
      if (onSelectCallback) onSelectCallback(selectedIndex);
      requestUpdate();
      return true;
    } else if (input.wasPressed(MappedInputManager::Button::Back)) {
      active = false;
      requestUpdate();
      return true;
    }
    return true;
  }

  bool processRender(GfxRenderer& renderer, const MappedInputManager& input) const {
    if (!active) return false;
    const auto popupLabels = input.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, popupLabels.btn1, popupLabels.btn2, popupLabels.btn3, popupLabels.btn4);
    render(renderer);
    renderer.displayBuffer();
    return true;
  }

  void render(const GfxRenderer& renderer) const {
    if (!active) return;
    GUI.drawOptionPopup(renderer, title.c_str(), ownedStrings, selectedIndex, leftAligned);
  }

  bool isActive() const { return active; }

 private:
  bool isDisabled(const int index) const {
    return index >= 0 && index < static_cast<int>(disabled.size()) && disabled[index];
  }

  // Next selectable row in `step` direction, wrapping. Returns the starting index
  // unchanged when every row is disabled, so a fully disabled pop-up cannot spin forever.
  int nextEnabled(const int from, const int step) const {
    const int count = static_cast<int>(ownedStrings.size());
    if (count <= 0) return from;
    int index = from;
    for (int tried = 0; tried < count; tried++) {
      index = (index + step + count) % count;
      if (!isDisabled(index)) return index;
    }
    return from;
  }

  struct Layout {
    Rect dialog{0, 0, 0, 0};
    std::vector<Rect> options;
  };

  // Text measurement is expensive and wasScreenTouchDown() is level-triggered, so the
  // layout is computed once per show() and cached rather than rebuilt every loop().
  const Layout& getLayout(const GfxRenderer& renderer) const {
    if (layoutValid) return layout;

    const auto& metrics = UITheme::getInstance().getMetrics();
    const auto pageWidth = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();
    const int optionFontId = metrics.optionPopupUseSmallFont ? UI_10_FONT_ID : UI_12_FONT_ID;
    const EpdFontFamily::Style optionStyle =
        metrics.optionPopupOptionFontBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;

    const int itemSpacing = metrics.optionPopupItemSpacing;
    const int innerPadding = metrics.optionPopupInnerPadding;
    const int selectionHPadding = metrics.optionPopupSelectionHPadding;
    const int selectionVPadding = metrics.optionPopupSelectionVPadding;

    const int optionLineHeight = renderer.getLineHeight(optionFontId);
    const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int rowHeight = optionLineHeight + selectionVPadding * 2;

    int maxTextWidth = renderer.getTextWidth(UI_12_FONT_ID, title.c_str(), EpdFontFamily::BOLD);
    for (const auto& opt : ownedStrings) {
      const int width = renderer.getTextWidth(optionFontId, opt.c_str(), optionStyle);
      if (width > maxTextWidth) maxTextWidth = width;
    }

    const int optionCount = static_cast<int>(ownedStrings.size());
    const int listHeight = rowHeight * optionCount + itemSpacing * (optionCount - 1);
    const int dialogW = std::min((maxTextWidth + innerPadding * 2 + selectionHPadding * 2) * 12 / 10,
                                 pageWidth - metrics.optionPopupDialogSideMargin * 2);
    const int contentHeight = titleLineHeight + metrics.optionPopupTitleGap + listHeight;
    const int dialogH = contentHeight + innerPadding * 2;
    const int dialogX = (pageWidth - dialogW) / 2;
    const int dialogY = (pageHeight - dialogH) / 2;
    const int itemRectX = dialogX + innerPadding;
    const int itemRectW = dialogW - innerPadding * 2;
    const int firstItemY = dialogY + innerPadding + titleLineHeight + metrics.optionPopupTitleGap;

    layout.dialog = Rect{dialogX, dialogY, dialogW, dialogH};
    layout.options.clear();
    layout.options.reserve(optionCount);
    for (int i = 0; i < optionCount; i++) {
      layout.options.push_back(Rect{itemRectX, firstItemY + i * (rowHeight + itemSpacing), itemRectW, rowHeight});
    }
    layoutValid = true;
    return layout;
  }

  static bool contains(const Rect& rect, const int x, const int y) {
    return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
  }

  bool active = false;
  std::string title;
  std::vector<std::string> ownedStrings;
  // Empty when every row is selectable, which is every caller except the Quick Menu.
  std::vector<bool> disabled;
  bool leftAligned = false;
  int selectedIndex = 0;
  std::function<void(int)> onSelectCallback;
  mutable Layout layout;
  mutable bool layoutValid = false;
};
