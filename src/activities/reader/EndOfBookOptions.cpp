#include "EndOfBookOptions.h"

#include <FreeInkUIGfxRenderer.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "ReaderUtils.h"
// ReaderUtils.h pulls in ActivityManager.h, which only forward-declares Activity while holding
// std::unique_ptr<Activity> members. Destroying that unique_ptr needs the complete type, so the
// definition must be visible here.
#include <FreeInkUICore.h>

#include "activities/Activity.h"
#include "components/ListChrome.h"
#include "components/RowHitTest.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"
#include "util/NextBookFinder.h"

namespace {
// Display name without the file extension, mirroring the file browser rows
std::string displayName(const std::string& filename) {
  const auto pos = filename.rfind('.');
  return filename.substr(0, pos);
}
}  // namespace

void EndOfBookOptions::loadOnce(const std::string& currentBookPath) {
  if (isLoaded.load(std::memory_order_acquire)) {
    return;
  }
  folder = FsHelpers::extractFolderPath(currentBookPath);
  names = NextBookFinder::findNextBooks(currentBookPath, MAX_SUGGESTIONS);
  selector = 0;
  // Release-publish so the main task, which gates all access on isLoaded, never
  // observes a partially built list
  isLoaded.store(true, std::memory_order_release);
}

bool EndOfBookOptions::menuActive() const { return isLoaded.load(std::memory_order_acquire) && !names.empty(); }

std::string EndOfBookOptions::fullPath(const size_t index) const {
  if (index >= names.size()) {
    return {};
  }
  return folder == "/" ? "/" + names[index] : folder + "/" + names[index];
}

EndOfBookOptions::Action EndOfBookOptions::handleMenuInput(const MappedInputManager& input, std::string* openPath) {
  // A tap on a row picks it and a second tap on that same row answers, the way Confirm
  // does in one press (components/TwoTapGate.h). The row list is the suggestions
  // followed by the Home entry, so the tapped index maps straight onto the selector.
  int tappedRow = 0;
  const auto rowTap = input.wasRowTapped(tappedRow);
  if (rowTap != MappedInputManager::RowTap::None && tappedRow >= 0 && tappedRow <= static_cast<int>(names.size())) {
    selector = tappedRow;
    if (rowTap == MappedInputManager::RowTap::Armed) return Action::Redraw;
    if (selector < static_cast<int>(names.size())) {
      if (openPath) *openPath = fullPath(selector);
      return Action::OpenBook;
    }
    return Action::GoHome;
  }

  if (input.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selector < static_cast<int>(names.size())) {
      if (openPath) {
        *openPath = fullPath(selector);
      }
      return Action::OpenBook;
    }
    return Action::GoHome;  // "Home" entry selected
  }

  // Short-press Back returns to the last page; a long press falls through to the
  // reader's own handler (file browser). Home is reached through the list's Home entry.
  if (input.wasReleased(MappedInputManager::Button::Back) && input.getHeldTime() < ReaderUtils::GO_HOME_MS) {
    return Action::LastPage;
  }

  // Selection movement on the standard list navigation buttons (side Up/Down plus front
  // Left/Right, orientation swap included), on the press like every other list. The
  // press that turned the final page already fired in the reader; its release cannot
  // double-fire here, because this screen arrives through a transition that arms the
  // input gate.
  const auto triggered = [&](const MappedInputManager::Button button) { return input.wasPressed(button); };
  const int itemCount = static_cast<int>(names.size()) + 1;  // + "Home" entry
  if (triggered(MappedInputManager::Button::NavPrevious)) {
    selector = ButtonNavigator::previousIndex(selector, itemCount);  // wraps to the bottom
    return Action::Redraw;
  }
  if (triggered(MappedInputManager::Button::NavNext)) {
    selector = ButtonNavigator::nextIndex(selector, itemCount);  // wraps to the top
    return Action::Redraw;
  }
  return Action::None;
}

void EndOfBookOptions::render(GfxRenderer& renderer, const MappedInputManager& input) const {
  namespace ct = freeink::ui::contents;
  // A title page: "The End" and, with suggestions, the books to go on with as rows.
  ListChrome chrome;
  chrome.title = tr(STR_END_OF_BOOK);
  chrome.backHint = tr(STR_BACK);
  chrome.confirmHint = tr(STR_OPEN);
  if (menuActive()) chrome.subHeader = tr(STR_EOB_CONTINUE_WITH);
  toContentsLook(chrome, input.hasTouch());
  if (!menuActive()) chrome.hints = false;
  drawListChromeTop(renderer, chrome);
  if (!menuActive()) return;

  // At most four short rows, so no scrolling: each title wraps over the lines it needs.
  const int font = UI_10_FONT_ID;
  const int lineHeight = renderer.getLineHeight(font);
  const int textX = ct::SIDE;
  const int textW = renderer.getScreenWidth() - ct::SIDE * 2;
  const int bottom = listChromeBands(renderer, chrome).contentBottom;
  row_hit::Rows& hitRows = row_hit::lastRows();
  hitRows.begin();
  int y = listChromeBands(renderer, chrome).contentTop;
  const int count = static_cast<int>(names.size()) + 1;  // + the Home entry
  for (int i = 0; i < count; ++i) {
    const std::string label = i < static_cast<int>(names.size()) ? displayName(names[i]) : tr(STR_EOB_HOME);
    const auto lines = BaseTheme::wrapUiText(renderer, label, textW, textW);
    const int height = ct::ROW_H + (static_cast<int>(lines.size()) - 1) * lineHeight;
    if (y + height > bottom) break;
    const bool selected = i == selector;
    int baseline = y + ct::ROW_BASELINE;
    if (selected) {
      const int x = textX - (ct::SIDE - ct::MARKER_X);
      const int cy = baseline - ct::MARKER_RAISE;
      const int xs[3] = {x, x, x + ct::MARKER_W};
      const int ys[3] = {cy - ct::MARKER_H / 2, cy + ct::MARKER_H / 2, cy};
      renderer.fillPolygon(xs, ys, 3, true);
    }
    for (const std::string& line : lines) {
      renderer.drawText(font, textX, baseline - renderer.getFontAscenderSize(font), line.c_str(), true,
                        selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      baseline += lineHeight;
    }
    hitRows.add(i, 0, y, renderer.getScreenWidth(), height);
    y += height;
  }
  drawListChromeBottom(renderer, input, chrome);
}
