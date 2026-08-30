#include "KeyboardEntryActivity.h"

#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>

#include "MappedInputManager.h"
#include "components/KeyboardFieldLayout.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {

constexpr fui::ActionId ACTION_KEY = 1;

// ---------------------------------------------------------------------------
// URL layers. The SDK builtin layouts have no URL variant (":", "/", ".", the
// snippet panel), so these are app-defined tables over the same public
// KeyboardLayout structs. URLs are ASCII, so the letter rows are EN-arranged
// regardless of UI language.
// ---------------------------------------------------------------------------

#define UK(label, output, value) \
  fui::KeyboardKey { label, output, fui::KeyKind::Normal, fui::StateNormal, value, 1, true, nullptr }
#define UKA(label, output, value, alt) \
  fui::KeyboardKey { label, output, fui::KeyKind::Normal, fui::StateNormal, value, 1, true, alt }
#define UKW(label, output, value, units) \
  fui::KeyboardKey { label, output, fui::KeyKind::Normal, fui::StateNormal, value, units, true, nullptr }
#define UKS(label, kind, value, units) \
  fui::KeyboardKey { label, nullptr, kind, fui::StateNormal, value, units, true, nullptr }

constexpr int16_t URL_PANEL_VALUE = -3;  // mirrors KeyboardEntryActivity::URL_PANEL_KEY

const fui::KeyboardKey URL_NUM_ROW[] = {UKA("1", "1", '1', "!"), UKA("2", "2", '2', "@"), UKA("3", "3", '3', "#"),
                                        UKA("4", "4", '4', "$"), UKA("5", "5", '5', "%"), UKA("6", "6", '6', "^"),
                                        UKA("7", "7", '7', "&"), UKA("8", "8", '8', "*"), UKA("9", "9", '9', "("),
                                        UKA("0", "0", '0', ")")};

const fui::KeyboardKey URL_ROW1[] = {UK("q", "q", 'q'), UK("w", "w", 'w'), UK("e", "e", 'e'), UK("r", "r", 'r'),
                                     UK("t", "t", 't'), UK("y", "y", 'y'), UK("u", "u", 'u'), UK("i", "i", 'i'),
                                     UK("o", "o", 'o'), UK("p", "p", 'p')};
const fui::KeyboardKey URL_ROW2[] = {UK("a", "a", 'a'), UK("s", "s", 's'), UK("d", "d", 'd'),
                                     UK("f", "f", 'f'), UK("g", "g", 'g'), UK("h", "h", 'h'),
                                     UK("j", "j", 'j'), UK("k", "k", 'k'), UK("l", "l", 'l')};
const fui::KeyboardKey URL_ROW3[] = {UKS("Shift", fui::KeyKind::Shift, fui::QWERTY_KEY_SHIFT, 2),
                                     UK("z", "z", 'z'),
                                     UK("x", "x", 'x'),
                                     UK("c", "c", 'c'),
                                     UK("v", "v", 'v'),
                                     UK("b", "b", 'b'),
                                     UK("n", "n", 'n'),
                                     UK("m", "m", 'm'),
                                     UKS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2)};
// URLs have no spaces, so the URL bottom row spends the space slot on ":",
// "/", "." and the snippet-panel toggle instead (the legacy keyboard did the
// same with its "URL" key).
const fui::KeyboardKey URL_BOTTOM[] = {UKS("?123", fui::KeyKind::Mode, fui::QWERTY_KEY_MODE, 2),
                                       UK(":", ":", ':'),
                                       UK("/", "/", '/'),
                                       UK(".", ".", '.'),
                                       UKW("URL", nullptr, URL_PANEL_VALUE, 2),
                                       UKS("OK", fui::KeyKind::Ok, fui::QWERTY_KEY_ENTER, 2)};

const fui::KeyboardKey URL_SHIFT_ROW1[] = {UK("Q", "Q", 'Q'), UK("W", "W", 'W'), UK("E", "E", 'E'), UK("R", "R", 'R'),
                                           UK("T", "T", 'T'), UK("Y", "Y", 'Y'), UK("U", "U", 'U'), UK("I", "I", 'I'),
                                           UK("O", "O", 'O'), UK("P", "P", 'P')};
const fui::KeyboardKey URL_SHIFT_ROW2[] = {UK("A", "A", 'A'), UK("S", "S", 'S'), UK("D", "D", 'D'),
                                           UK("F", "F", 'F'), UK("G", "G", 'G'), UK("H", "H", 'H'),
                                           UK("J", "J", 'J'), UK("K", "K", 'K'), UK("L", "L", 'L')};
const fui::KeyboardKey URL_SHIFT_ROW3[] = {UKS("Shift", fui::KeyKind::Shift, fui::QWERTY_KEY_SHIFT, 2),
                                           UK("Z", "Z", 'Z'),
                                           UK("X", "X", 'X'),
                                           UK("C", "C", 'C'),
                                           UK("V", "V", 'V'),
                                           UK("B", "B", 'B'),
                                           UK("N", "N", 'N'),
                                           UK("M", "M", 'M'),
                                           UKS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2)};

// Snippet keys: multi-character outputs, stable ids above the localized-key
// range so they never collide with layout key ids.
const fui::KeyboardKey URL_SNIP_ROW1[] = {UK("https://", "https://", 2001), UK("www.", "www.", 2002),
                                          UK(".com", ".com", 2003)};
const fui::KeyboardKey URL_SNIP_ROW2[] = {UK("http://", "http://", 2004), UK("192.168.", "192.168.", 2005),
                                          UK(".org", ".org", 2006)};
const fui::KeyboardKey URL_SNIP_ROW3[] = {UK("/opds", "/opds", 2007), UK(":8080", ":8080", 2008),
                                          UK(".net", ".net", 2009)};
const fui::KeyboardKey URL_SNIP_BOTTOM[] = {UKS("abc", fui::KeyKind::Mode, fui::QWERTY_KEY_MODE, 2),
                                            UKW("URL", nullptr, URL_PANEL_VALUE, 2),
                                            UKS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 2),
                                            UKS("OK", fui::KeyKind::Ok, fui::QWERTY_KEY_ENTER, 2)};

#undef UK
#undef UKA
#undef UKW
#undef UKS

const fui::KeyboardRow URL_ROWS[] = {
    {URL_NUM_ROW, 10, 0}, {URL_ROW1, 10, 0}, {URL_ROW2, 9, 1}, {URL_ROW3, 9, 0}, {URL_BOTTOM, 6, 0}};
const fui::KeyboardRow URL_SHIFT_ROWS[] = {
    {URL_NUM_ROW, 10, 0}, {URL_SHIFT_ROW1, 10, 0}, {URL_SHIFT_ROW2, 9, 1}, {URL_SHIFT_ROW3, 9, 0}, {URL_BOTTOM, 6, 0}};
const fui::KeyboardRow URL_SNIP_ROWS[] = {
    {URL_SNIP_ROW1, 3, 0}, {URL_SNIP_ROW2, 3, 0}, {URL_SNIP_ROW3, 3, 0}, {URL_SNIP_BOTTOM, 4, 0}};

const fui::KeyboardLayout URL_LAYOUT{URL_ROWS, 5};
const fui::KeyboardLayout URL_SHIFT_LAYOUT{URL_SHIFT_ROWS, 5};
const fui::KeyboardLayout URL_SNIPPET_LAYOUT{URL_SNIP_ROWS, 4};

fui::KeyboardLayoutId layoutForLanguage(const Language language) {
  switch (language) {
    case Language::FR:
      return fui::KeyboardLayoutId::AzertyFr;
    case Language::DE:
      return fui::KeyboardLayoutId::QwertzDe;
    case Language::ES:
      return fui::KeyboardLayoutId::SpanishEs;
    default:
      return fui::KeyboardLayoutId::QwertyEn;
  }
}

}  // namespace

void KeyboardEntryActivity::onEnter() {
  Activity::onEnter();
  cursorPos = text.length();
  // URL layers are EN-arranged app tables; everything else follows the UI
  // language.
  layoutId = inputType == InputType::Url ? fui::KeyboardLayoutId::QwertyEn : layoutForLanguage(I18N.getLanguage());
  shifted = false;
  symbols = false;
  urlPanel = false;
  cursorMode = false;
  togglePos = false;
  passwordVisible = false;
  selRow = 0;
  selCol = 0;
  delPressCount = 0;
  hintVisible = false;
  hintShowTime = 0;
  rightHeld = false;
  rightLongHandled = false;
  savedCursorPos = 0;
  rightStartCursorPos = 0;
  touchRouter.reset();
  touchRouter.holdMs = TOUCH_LONG_PRESS_MS;
  touchRouter.overrideHoldMs = TOUCH_DEL_LONG_PRESS_MS;
  interactionsReady = false;
  requestUpdate();
}

void KeyboardEntryActivity::onExit() { Activity::onExit(); }

const fui::KeyboardLayout& KeyboardEntryActivity::currentLayout() const {
  if (symbols) return fui::builtinKeyboardLayout(layoutId, shifted, true);
  if (inputType == InputType::Url) {
    if (urlPanel) return URL_SNIPPET_LAYOUT;
    return shifted ? URL_SHIFT_LAYOUT : URL_LAYOUT;
  }
  return fui::builtinKeyboardLayout(layoutId, shifted, false, /*numberRow=*/true);
}

const fui::KeyboardKey* KeyboardEntryActivity::selectedKey() const {
  const fui::KeyboardLayout& layout = currentLayout();
  if (selRow < 0 || selRow >= layout.rowCount) return nullptr;
  const fui::KeyboardRow& row = layout.rows[selRow];
  if (selCol < 0 || selCol >= row.count) return nullptr;
  return &row.keys[selCol];
}

int KeyboardEntryActivity::selectedLogicalIndex() const {
  const fui::KeyboardLayout& layout = currentLayout();
  int index = 0;
  for (int r = 0; r < selRow && r < layout.rowCount; r++) {
    index += layout.rows[r].count;
  }
  return index + selCol;
}

void KeyboardEntryActivity::clampSelection() {
  const fui::KeyboardLayout& layout = currentLayout();
  if (layout.rowCount == 0) {
    selRow = 0;
    selCol = 0;
    return;
  }
  if (selRow < 0) selRow = 0;
  if (selRow >= layout.rowCount) selRow = layout.rowCount - 1;
  const int cols = layout.rows[selRow].count;
  if (selCol < 0) selCol = 0;
  if (selCol >= cols) selCol = cols > 0 ? cols - 1 : 0;
}

void KeyboardEntryActivity::moveSelectionRow(const int delta) {
  const fui::KeyboardLayout& layout = currentLayout();
  if (layout.rowCount == 0) return;
  const int oldCols = selRow < layout.rowCount ? layout.rows[selRow].count : 1;
  selRow = (selRow + delta + layout.rowCount) % layout.rowCount;
  const int newCols = layout.rows[selRow].count;
  // Proportional column mapping keeps vertical travel intuitive between rows
  // of different key counts (e.g. a 10-key letter row over a 6-key bottom row).
  if (oldCols > 0 && newCols > 0 && oldCols != newCols) {
    selCol = selCol * newCols / oldCols;
  }
  clampSelection();
}

void KeyboardEntryActivity::moveSelectionCol(const int delta) {
  const fui::KeyboardLayout& layout = currentLayout();
  if (selRow < 0 || selRow >= layout.rowCount) return;
  const int cols = layout.rows[selRow].count;
  if (cols <= 0) return;
  selCol = (selCol + delta + cols) % cols;
}

bool KeyboardEntryActivity::syncSelectionToValue(const int16_t value) {
  const fui::KeyboardLayout& layout = currentLayout();
  for (int r = 0; r < layout.rowCount; r++) {
    for (int c = 0; c < layout.rows[r].count; c++) {
      if (layout.rows[r].keys[c].value == value) {
        selRow = r;
        selCol = c;
        return true;
      }
    }
  }
  return false;
}

size_t KeyboardEntryActivity::utf8Prev(const std::string& s, size_t pos) {
  if (pos == 0) return 0;
  pos--;
  while (pos > 0 && (static_cast<uint8_t>(s[pos]) & 0xC0) == 0x80) pos--;
  return pos;
}

size_t KeyboardEntryActivity::utf8Next(const std::string& s, size_t pos) {
  if (pos >= s.length()) return s.length();
  pos++;
  while (pos < s.length() && (static_cast<uint8_t>(s[pos]) & 0xC0) == 0x80) pos++;
  return pos;
}

void KeyboardEntryActivity::insertUtf8(const char* out) {
  if (!out || !*out) return;
  const size_t n = strlen(out);
  if (maxLength != 0 && text.length() + n > maxLength) return;
  if (cursorPos > text.length()) cursorPos = text.length();
  text.insert(cursorPos, out, n);
  cursorPos += n;
}

bool KeyboardEntryActivity::backspaceUtf8() {
  if (text.empty() || cursorPos == 0) return false;
  const size_t prev = utf8Prev(text, cursorPos);
  text.erase(prev, cursorPos - prev);
  cursorPos = prev;
  return true;
}

bool KeyboardEntryActivity::activateValue(const int16_t value, const bool longPress) {
  switch (value) {
    case fui::QWERTY_KEY_SHIFT:
      delPressCount = 0;
      hintVisible = false;
      // Letters: case toggle. Symbols: pages between "?123" and "#+=".
      shifted = !shifted;
      clampSelection();
      return true;
    case fui::QWERTY_KEY_MODE:
      delPressCount = 0;
      hintVisible = false;
      if (urlPanel) {
        urlPanel = false;
      } else {
        symbols = !symbols;
        shifted = false;
      }
      clampSelection();
      return true;
    case URL_PANEL_KEY:
      delPressCount = 0;
      hintVisible = false;
      urlPanel = !urlPanel;
      symbols = false;
      shifted = false;
      clampSelection();
      return true;
    case fui::QWERTY_KEY_ENTER:
      onComplete(text);
      return false;
    case fui::QWERTY_KEY_BACKSPACE:
      if (longPress) {
        text.clear();
        cursorPos = 0;
        return true;
      }
      delPressCount++;
      if (delPressCount >= 2) {
        hintVisible = true;
        hintShowTime = millis();
      }
      backspaceUtf8();
      return true;
    default: {
      delPressCount = 0;
      hintVisible = false;
      const fui::KeyboardLayout& layer = currentLayout();
      // keyboardAltOutputFor covers explicit alts and the letter case-flip.
      const char* out = longPress ? fui::keyboardAltOutputFor(layer, value) : nullptr;
      if (!out) out = fui::keyboardOutputFor(layer, value);
      if (!out) return false;
      insertUtf8(out);
      if (shifted && !symbols) {
        shifted = false;  // shift auto-releases after one character
        clampSelection();
      }
      return true;
    }
  }
}

bool KeyboardEntryActivity::clearAllOrAltOnSelected() {
  const fui::KeyboardKey* key = selectedKey();
  if (!key) return false;
  if (key->value == fui::QWERTY_KEY_BACKSPACE) {
    text.clear();
    cursorPos = 0;
    return true;
  }
  // Explicit alts and the letter case-flip, same as touch long-press.
  const char* alt = fui::keyboardAltOutputFor(currentLayout(), key->value);
  if (alt) {
    insertUtf8(alt);
    return true;
  }
  return false;
}

std::string KeyboardEntryActivity::displayTextForCurrentState() const {
  std::string displayText = text;
  if (inputType != InputType::Password || passwordVisible) {
    return displayText;
  }

  size_t revealPos;
  if (cursorMode) {
    revealPos = text.length();  // no reveal in displayText; block draws actual char directly
  } else {
    revealPos = (text.length() > 0 && cursorPos > 0) ? cursorPos - 1 : std::string::npos;
  }
  for (size_t i = 0; i < displayText.length(); i++) {
    if (i != revealPos) {
      displayText[i] = '*';
    }
  }
  return displayText;
}

int KeyboardEntryActivity::measureRange(std::string& s, const int start, const int end) const {
  if (end <= start) return 0;
  // s[end] is writable even at s.length() (the terminator slot); only '\0' may
  // be written there, which is exactly what the measurement needs.
  const char saved = s[end];
  s[end] = '\0';
  const int width = renderer.getTextAdvanceX(UI_12_FONT_ID, s.c_str() + start, EpdFontFamily::REGULAR);
  s[end] = saved;
  return width;
}

int KeyboardEntryActivity::lineBreakEnd(std::string& s, const int start, const int maxWidth) const {
  const int len = static_cast<int>(s.length());
  if (measureRange(s, start, len) <= maxWidth) return len;
  int lo = start + 1;
  int hi = len - 1;
  int best = start + 1;
  while (lo <= hi) {
    const int mid = lo + (hi - lo) / 2;
    if (measureRange(s, start, mid) <= maxWidth) {
      best = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return best;
}

keyboard_field::Metrics KeyboardEntryActivity::fieldMetrics() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  keyboard_field::Metrics field;
  field.pageWidth = renderer.getScreenWidth();
  field.sideHintsWidth = metrics.sideButtonHintsWidth;
  field.reserveSideHints = gpio.deviceIsX3();
  field.widthPercent = metrics.keyboardTextFieldWidthPercent;
  if (inputType == InputType::Password) {
    // The toggle keeps its own room whichever label it is showing, so the text
    // does not reflow when the password is revealed.
    const int toggleGap = 4;
    field.toggleReserve = std::max(renderer.getTextWidth(UI_12_FONT_ID, "[abc]"),
                                   renderer.getTextWidth(UI_12_FONT_ID, "[***]")) +
                          toggleGap;
  }
  return field;
}

bool KeyboardEntryActivity::cursorPositionFromPoint(const int x, const int y, size_t& position) const {
  // Key taps are the overwhelmingly common case; they land on the keyboard,
  // never the text field, so skip the wrap/measure work entirely.
  if (y >= keyboardRect().y) return false;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const keyboard_field::Metrics field = fieldMetrics();

  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int inputStartY = keyboard_field::fieldTop(metrics.topPadding, metrics.headerHeight, metrics.verticalSpacing,
                                                   metrics.keyboardVerticalOffset);

  const int effectiveMargin = keyboard_field::marginFor(field);
  const int maxLineWidth = keyboard_field::textWidthFor(field);
  const bool centerText = metrics.keyboardCenteredText;
  std::string displayText = displayTextForCurrentState();

  int lineStartIdx = 0;
  int lineY = inputStartY;
  int lastLineStartIdx = 0;
  int lastLineEndIdx = static_cast<int>(displayText.length());
  int lastLineStartX = effectiveMargin;
  int lastLineWidth = 0;

  while (true) {
    const int lineEndIdx = lineBreakEnd(displayText, lineStartIdx, maxLineWidth);
    const int textWidth = measureRange(displayText, lineStartIdx, lineEndIdx);
    const int lineStartX = keyboard_field::lineStartX(field, textWidth, centerText);
    lastLineStartIdx = lineStartIdx;
    lastLineEndIdx = lineEndIdx;
    lastLineStartX = lineStartX;
    lastLineWidth = textWidth;

    if (y >= lineY - metrics.verticalSpacing && y < lineY + lineHeight + metrics.verticalSpacing) {
      if (x <= lineStartX) {
        position = static_cast<size_t>(lineStartIdx);
        return true;
      }
      if (x >= lineStartX + textWidth) {
        position = static_cast<size_t>(lineEndIdx);
        return true;
      }

      int previousWidth = 0;
      for (int i = lineStartIdx; i < lineEndIdx; i++) {
        const int nextWidth = measureRange(displayText, lineStartIdx, i + 1);
        const int midpoint = lineStartX + previousWidth + (nextWidth - previousWidth) / 2;
        if (x < midpoint) {
          position = static_cast<size_t>(i);
          return true;
        }
        previousWidth = nextWidth;
      }
      position = static_cast<size_t>(lineEndIdx);
      return true;
    }

    if (lineEndIdx == static_cast<int>(displayText.length())) {
      break;
    }

    lineY += lineHeight;
    lineStartIdx = lineEndIdx;
  }

  const int underlineBottom = lineY + lineHeight + metrics.verticalSpacing + 8;
  if (y >= inputStartY - metrics.verticalSpacing && y < underlineBottom && x >= effectiveMargin &&
      x < effectiveMargin + maxLineWidth + field.toggleReserve) {
    position = x < lastLineStartX + lastLineWidth ? static_cast<size_t>(lastLineStartIdx)
                                                  : static_cast<size_t>(lastLineEndIdx);
    return true;
  }

  return false;
}

fui::Rect KeyboardEntryActivity::keyboardRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int rows = currentLayout().rowCount;
  const int gap = metrics.keyboardKeySpacing;
  const int height = rows * metrics.keyboardKeyHeight + (rows > 1 ? (rows - 1) * gap : 0);
  const int width = pageWidth * metrics.keyboardWidthPercent / 100;
  const int x = (pageWidth - width) / 2;
  const int y =
      pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - height + metrics.keyboardVerticalOffset;
  return fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(width),
                   static_cast<int16_t>(height)};
}

void KeyboardEntryActivity::loop() {
  if (!cursorMode && mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    upHeld = true;
    upLongHandled = false;
  }

  if (upHeld && !upLongHandled && mappedInput.isPressed(MappedInputManager::Button::Up) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    cursorMode = true;
    upLongHandled = true;
    hintVisible = true;
    hintShowTime = millis();
    requestUpdate();
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (upHeld && !upLongHandled && !cursorMode) {
      moveSelectionRow(-1);
      requestUpdate();
    }
    upHeld = false;
    upLongHandled = false;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    downHeld = true;
    if (cursorMode) {
      togglePos = false;
      passwordVisible = false;
      cursorMode = false;
      hintVisible = false;
      downLongHandled = true;
      requestUpdate();
    } else {
      downLongHandled = false;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (downHeld && !downLongHandled && !cursorMode) {
      moveSelectionRow(1);
      requestUpdate();
    }
    downHeld = false;
    downLongHandled = false;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] {
    if (cursorMode) return;
    moveSelectionCol(-1);
    requestUpdate();
  });

  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    if (cursorMode) {
      if (togglePos) {
        cursorPos = savedCursorPos;
        togglePos = false;
        requestUpdate();
      } else if (cursorPos > 0) {
        cursorPos = utf8Prev(text, cursorPos);
        requestUpdate();
      }
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    if (cursorMode && inputType == InputType::Password && !togglePos) {
      rightHeld = true;
      rightLongHandled = false;
      rightStartCursorPos = cursorPos;
    }
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] {
    if (cursorMode) return;
    moveSelectionCol(1);
    requestUpdate();
  });

  if (rightHeld && !rightLongHandled && mappedInput.isPressed(MappedInputManager::Button::Right) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    if (cursorMode && inputType == InputType::Password && !togglePos) {
      savedCursorPos = rightStartCursorPos;
      togglePos = true;
      rightLongHandled = true;
      requestUpdate();
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    if (cursorMode && inputType == InputType::Password) {
      rightHeld = false;
      rightLongHandled = false;
    }
    if (cursorMode && !togglePos && cursorPos < text.length()) {
      cursorPos = utf8Next(text, cursorPos);
      requestUpdate();
    }
    if (cursorMode) return;
    rightHeld = false;
    rightLongHandled = false;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    confirmHeld = true;
    confirmLongHandled = false;
  }

  const fui::KeyboardKey* selKey = selectedKey();
  const bool selectedDel = selKey && selKey->value == fui::QWERTY_KEY_BACKSPACE;

  if (confirmHeld && !confirmLongHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() > DEL_LONG_PRESS_MS && selectedDel) {
    clearAllOrAltOnSelected();
    confirmLongHandled = true;
    requestUpdate();
  }

  if (confirmHeld && !confirmLongHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    if (!selectedDel && clearAllOrAltOnSelected()) {
      requestUpdate();
      confirmLongHandled = true;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (confirmHeld && !confirmLongHandled && !cursorMode) {
      if (selKey && activateValue(selKey->value, false)) {
        requestUpdate();
      }
    } else if (confirmHeld && !confirmLongHandled && cursorMode && inputType == InputType::Password && togglePos) {
      passwordVisible = !passwordVisible;
      requestUpdate();
    }
    confirmHeld = false;
    confirmLongHandled = false;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onCancel();
  }

  if (hintVisible && !cursorMode && millis() - hintShowTime > 4000) {
    hintVisible = false;
    requestUpdate();
  }
}

void KeyboardEntryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title.c_str());

  // The field draws through the same target the keyboard does, so its type and
  // its ink come from the theme rather than from a font id named here. The
  // frame's constructor clears the interaction table, so the routing gate has
  // to close before it is built, not later when the keys are drawn.
  interactionsReady = false;
  fui::GfxRendererTarget target(renderer);
  target.setFont(fui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);
  target.setFont(fui::GfxRendererTarget::FONT_BODY, UI_12_FONT_ID);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  fui::Frame<48> frame(target, device, noInput, interactions);

  fui::TextStyle fieldStyle;
  fieldStyle.font = fui::GfxRendererTarget::FONT_BODY;
  fui::TextStyle smallStyle;
  smallStyle.font = fui::GfxRendererTarget::FONT_SMALL;
  smallStyle.align = fui::TextAlign::Center;
  const auto drawFieldText = [&](const int x, const int y, const char* content, const bool inverted = false) {
    if (content == nullptr || content[0] == '\0') return;
    fui::TextStyle style = fieldStyle;
    style.inverted = inverted;
    const int16_t width = target.measureText(style.font, content, style).width;
    target.text(fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(width + 1),
                          static_cast<int16_t>(target.lineHeight(style.font))},
                content, style);
  };

  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int inputStartY = keyboard_field::fieldTop(metrics.topPadding, metrics.headerHeight, metrics.verticalSpacing,
                                                   metrics.keyboardVerticalOffset);
  int inputHeight = 0;

  std::string displayText = displayTextForCurrentState();

  const bool isPassword = (inputType == InputType::Password);
  const keyboard_field::Metrics field = fieldMetrics();
  const int effectiveMargin = keyboard_field::marginFor(field);
  const int maxLineWidth = keyboard_field::textWidthFor(field);
  const bool centerText = metrics.keyboardCenteredText;

  int cursorCharWidth = 6;
  if (cursorPos < text.length()) {
    int w = renderer.getTextWidth(UI_12_FONT_ID, text.substr(cursorPos, 1).c_str());
    if (w > cursorCharWidth) cursorCharWidth = w;
  }

  int lineStartIdx = 0;
  int textWidth = 0;
  int cursorPixelX = effectiveMargin;
  int cursorLineY = inputStartY;
  bool cursorDrawn = false;

  while (true) {
    const int lineEndIdx = lineBreakEnd(displayText, lineStartIdx, maxLineWidth);
    const std::string lineText = displayText.substr(lineStartIdx, lineEndIdx - lineStartIdx);
    textWidth = renderer.getTextAdvanceX(UI_12_FONT_ID, lineText.c_str(), EpdFontFamily::REGULAR);
    {
      const bool isLastLine = (lineEndIdx == static_cast<int>(displayText.length()));
      bool isCursorLine = false;
      if (!cursorDrawn && cursorPos >= lineStartIdx &&
          (isLastLine ? cursorPos <= lineEndIdx : cursorPos < lineEndIdx)) {
        std::string beforeCursor;
        if (isPassword && !passwordVisible && cursorMode) {
          beforeCursor = std::string(cursorPos - lineStartIdx, '*');
        } else {
          beforeCursor = displayText.substr(lineStartIdx, cursorPos - lineStartIdx);
        }
        int beforeWidth = renderer.getTextAdvanceX(UI_12_FONT_ID, beforeCursor.c_str(), EpdFontFamily::REGULAR);
        int kernOffset = 0;
        if (cursorPos < displayText.length()) {
          std::string beforeAndCursor = beforeCursor + displayText.substr(cursorPos, 1);
          int beforeAndCursorWidth =
              renderer.getTextAdvanceX(UI_12_FONT_ID, beforeAndCursor.c_str(), EpdFontFamily::REGULAR);
          int charAdvance =
              renderer.getTextAdvanceX(UI_12_FONT_ID, displayText.substr(cursorPos, 1).c_str(), EpdFontFamily::REGULAR);
          kernOffset = beforeAndCursorWidth - beforeWidth - charAdvance;
        }
        cursorPixelX = keyboard_field::lineStartX(field, textWidth, centerText) + beforeWidth + kernOffset;
        cursorLineY = inputStartY + inputHeight;
        cursorDrawn = true;
        isCursorLine = true;
      }

      const int lineStartX = keyboard_field::lineStartX(field, textWidth, centerText);
      if (isCursorLine && cursorMode && isPassword && !passwordVisible && !togglePos) {
        // Draw text in 3 parts to avoid block cursor overflowing onto next char.
        // displayText uses '*' for all chars; actual char may be wider than '*'.
        // Part 1: chars before cursor position
        const std::string part1 = displayText.substr(lineStartIdx, cursorPos - lineStartIdx);
        drawFieldText(lineStartX, inputStartY + inputHeight, part1.c_str());
        // Part 2: skip cursor slot (block + actual char drawn later)
        // Part 3: chars after cursor position (skip char under cursor), starting at cursorPixelX + cursorCharWidth
        const int afterStart = static_cast<int>(cursorPos) + (cursorPos < text.length() ? 1 : 0);
        const int afterEnd = lineEndIdx;
        if (afterStart < afterEnd) {
          const std::string part3 = displayText.substr(afterStart, afterEnd - afterStart);
          drawFieldText(cursorPixelX + cursorCharWidth, inputStartY + inputHeight, part3.c_str());
        }
      } else {
        drawFieldText(lineStartX, inputStartY + inputHeight, lineText.c_str());
      }
      if (lineEndIdx == static_cast<int>(displayText.length())) {
        break;
      }

      inputHeight += lineHeight;
      lineStartIdx = lineEndIdx;
    }
  }

  const int fieldWidth = (inputHeight > 0) ? maxLineWidth : textWidth;
  const int lineMargin = effectiveMargin;
  GUI.drawTextField(renderer, Rect{0, inputStartY, pageWidth, inputHeight}, fieldWidth, cursorMode, lineMargin,
                    pageWidth - 2 * lineMargin);

  const auto fillRect = [&](const int x, const int y, const int w, const int h) {
    target.fill(fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(w),
                          static_cast<int16_t>(h)},
                fui::Paint::solid(fui::Color::Black));
  };

  if (cursorMode && !togglePos && cursorPos <= displayText.length()) {
    const keyboard_field::Rect block = keyboard_field::blockCursor(cursorPixelX, cursorLineY, cursorCharWidth,
                                                                   lineHeight);
    fillRect(block.x, block.y, block.width, block.height);
    if (cursorPos < text.length()) {
      // The character under the block is drawn in reverse, so the letter being
      // edited is still readable inside it.
      const char buf[2] = {text[cursorPos], '\0'};
      drawFieldText(cursorPixelX, cursorLineY, buf, /*inverted=*/true);
    }
  } else if (cursorPos <= displayText.length()) {
    // An I-beam: a stem with a serif at each end, so a cursor between two
    // characters is not read as a thin letter.
    static constexpr int serifW = 3;
    const int cX = cursorPixelX;
    const int cY = cursorLineY;
    const int cBottom = cursorLineY + lineHeight - 1;
    const auto line = [&](const int x0, const int y0, const int x1, const int y1) {
      target.line(fui::Point{static_cast<int16_t>(x0), static_cast<int16_t>(y0)},
                  fui::Point{static_cast<int16_t>(x1), static_cast<int16_t>(y1)}, 2,
                  fui::Paint::solid(fui::Color::Black));
    };
    fillRect(cX, cY, 2, lineHeight);
    line(cX - serifW, cY, cX - 1, cY);
    line(cX + 1, cY, cX + serifW, cY);
    line(cX - serifW, cBottom, cX - 1, cBottom);
    line(cX + 1, cBottom, cX + serifW, cBottom);
  }

  if (isPassword) {
    const char* toggleLabel = passwordVisible ? "[***]" : "[abc]";
    const int toggleWidth = renderer.getTextWidth(UI_12_FONT_ID, toggleLabel);
    const int toggleLeft = keyboard_field::toggleX(field, toggleWidth);
    const int toggleY = inputStartY + inputHeight;
    const bool toggleSelected = cursorMode && togglePos;

    if (toggleSelected) {
      // Hugs the label on all four sides, one pixel out.
      fillRect(toggleLeft - 1, toggleY - 1, toggleWidth + 2, lineHeight + 2);
      drawFieldText(toggleLeft, toggleY, toggleLabel, /*inverted=*/true);
    } else {
      drawFieldText(toggleLeft, toggleY, toggleLabel);
    }
  }

  const auto drawCentredSmall = [&](const char* content, const int y) {
    if (content == nullptr || content[0] == '\0') return;
    target.text(fui::Rect{0, static_cast<int16_t>(y), static_cast<int16_t>(pageWidth),
                          static_cast<int16_t>(target.lineHeight(smallStyle.font))},
                content, smallStyle);
  };

  if (hintVisible && !text.empty()) {
    const int hintLh = renderer.getLineHeight(SMALL_FONT_ID);
    const int underlineY = inputStartY + inputHeight + lineHeight + metrics.verticalSpacing;
    const int hintY = underlineY + 4;
    if (cursorMode) {
      int hintLineY = hintY;
      if (inputType == InputType::Password && togglePos) {
        drawCentredSmall(
            passwordVisible ? tr(STR_KB_HINT_TOGGLE_HIDE_PASSWORD) : tr(STR_KB_HINT_TOGGLE_SHOW_PASSWORD), hintLineY);
        hintLineY += hintLh;
        drawCentredSmall(tr(STR_KB_HINT_RETURN_CURSOR), hintLineY);
      } else {
        drawCentredSmall(tr(STR_KB_HINT_MOVE_CURSOR), hintLineY);
        hintLineY += hintLh;
        if (inputType == InputType::Password) {
          drawCentredSmall(passwordVisible ? tr(STR_KB_HINT_HIDE_PASSWORD) : tr(STR_KB_HINT_SHOW_PASSWORD), hintLineY);
        }
      }
    } else {
      drawCentredSmall(tr(STR_KB_HINT_EDIT_ENTRY), hintY);
    }
  }

  const fui::Rect kbRect = keyboardRect();

  const int tipsLh = renderer.getLineHeight(SMALL_FONT_ID);
  const int underlineBottom = inputStartY + inputHeight + lineHeight + metrics.verticalSpacing + 4;
  auto drawTip = [&](const char* tip, const int y) { drawCentredSmall(tip, y); };

  int tipCount = 0;
  if (cursorMode) {
    tipCount = 1;
  } else if (urlPanel) {
    tipCount = 1 + (!text.empty() ? 1 : 0);
  } else if (symbols) {
    tipCount = !text.empty() ? 1 : 0;
  } else {
    tipCount = 1 + (inputType == InputType::Url ? 1 : 0) + (!text.empty() ? 1 : 0);
  }

  if (tipCount > 0) {
    int y = (underlineBottom + kbRect.y) / 2 - (tipCount + 1) * tipsLh / 2;
    drawTip(tr(STR_KB_TIPS), y);
    y += tipsLh;
    if (cursorMode) {
      drawTip(tr(STR_KB_HINT_RETURN_KEYBOARD), y);
    } else if (urlPanel) {
      drawTip(tr(STR_KB_HINT_EXIT_URL_MODE), y);
      y += tipsLh;
      if (!text.empty()) {
        drawTip(tr(STR_KB_HINT_CLEAR_TEXT), y);
      }
    } else if (symbols) {
      if (!text.empty()) {
        drawTip(tr(STR_KB_HINT_CLEAR_TEXT), y);
      }
    } else {
      const char* altCharTip;
      if (inputType == InputType::Url) {
        altCharTip = tr(STR_KB_HINT_SECONDARY_CHAR);
      } else if (shifted) {
        altCharTip = tr(STR_KB_HINT_LOWER_SECONDARY);
      } else {
        altCharTip = tr(STR_KB_HINT_UPPER_SECONDARY);
      }
      drawTip(altCharTip, y);
      y += tipsLh;
      if (inputType == InputType::Url) {
        drawTip(tr(STR_KB_HINT_URL_SNIPPETS), y);
        y += tipsLh;
      }
      if (!text.empty()) {
        drawTip(tr(STR_KB_HINT_CLEAR_TEXT), y);
      }
    }
  }

  // The FreeInkUI keyboard draws the keys and registers their hit rects into
  // `interactions`; loop() routes touch snapshots against that table.
  fui::KeyboardProps props;
  const fui::KeyboardLayout& layout = currentLayout();
  props.layout = &layout;
  props.keyAction = ACTION_KEY;  // one action id; loop() dispatches on key value
  props.okLabel = tr(STR_OK_BUTTON);
  props.shiftLabel = tr(STR_KEY_SHIFT);
  // Match the label to the layer the mode key leads back from: the symbols
  // layer and the URL snippet panel both label it "abc" in the static tables.
  props.modeLabel =
      (symbols || (inputType == InputType::Url && urlPanel)) ? tr(STR_KEY_MODE_ABC) : tr(STR_KEY_MODE_SYMBOLS);
  props.inputMask = static_cast<uint16_t>(fui::InputTouch | fui::InputLongPress);
  props.selectedIndex = cursorMode ? -1 : static_cast<int16_t>(selectedLogicalIndex());
  props.labelText.font = fui::GfxRendererTarget::FONT_BODY;
  props.altText.font = fui::GfxRendererTarget::FONT_SMALL;
  props.gap = static_cast<int16_t>(metrics.keyboardKeySpacing);
  props.padding = fui::Insets{0, 0, 0, 0};
  // Fingers land low on the bottom row (occlusion) and there is no key below
  // to catch the miss — extend its hit band down to the button hints bar.
  const int hintsTop = renderer.getScreenHeight() - metrics.buttonHintsHeight;
  props.bottomHitOverflow = static_cast<int16_t>(std::max(0, hintsTop - (kbRect.y + kbRect.height)));
  fui::keyboard(frame, kbRect, props);
  interactionsReady = true;

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  GUI.drawSideButtonHints(renderer, ">", "<");

  renderer.displayBuffer();
}

void KeyboardEntryActivity::onComplete(std::string text) {
  setResult(KeyboardResult{std::move(text)});
  finish();
}

void KeyboardEntryActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
