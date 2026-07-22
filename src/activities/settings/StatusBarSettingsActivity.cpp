#include "StatusBarSettingsActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>

#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Item ids in display order. Clock is X3-only and is filtered out in onEnter().
enum ItemId {
  ITEM_ENABLED,         // master on/off (toggle)
  ITEM_BATTERY,         // anchor
  ITEM_CLOCK,           // anchor (X3 only)
  ITEM_TITLE,           // anchor
  ITEM_TITLE_SOURCE,    // Book / Chapter (cycle)
  ITEM_TITLE_TRUNCATE,  // On / Off (toggle)
  ITEM_PAGE,            // anchor
  ITEM_PAGE_FORMAT,     // N/M | N left (cycle)
  ITEM_BOOK_PCT,        // anchor
  ITEM_CHAPTER_PCT,     // anchor
  ITEM_CHAPTER_NUM,     // anchor
  ITEM_BOOK_BAR,        // Off / Top / Bottom (cycle)
  ITEM_CHAPTER_BAR,     // Off / Top / Bottom (cycle)
  ITEM_BAR_THICKNESS,   // Slim / Medium / Fat (cycle)
  ITEM_ID_COUNT
};

StrId itemLabel(int id) {
  switch (id) {
    case ITEM_ENABLED:
      return StrId::STR_STATUS_BAR;
    case ITEM_BATTERY:
      return StrId::STR_BATTERY;
    case ITEM_CLOCK:
      return StrId::STR_CLOCK;
    case ITEM_TITLE:
      return StrId::STR_TITLE;
    case ITEM_TITLE_SOURCE:
      return StrId::STR_TITLE_SOURCE;
    case ITEM_TITLE_TRUNCATE:
      return StrId::STR_TRUNCATE_TITLE;
    case ITEM_PAGE:
      return StrId::STR_PAGE_IN_CHAPTER;
    case ITEM_PAGE_FORMAT:
      return StrId::STR_PAGE_FORMAT;
    case ITEM_BOOK_PCT:
      return StrId::STR_BOOK_PERCENT;
    case ITEM_CHAPTER_PCT:
      return StrId::STR_CHAPTER_PERCENT;
    case ITEM_CHAPTER_NUM:
      return StrId::STR_CHAPTER_NUMBER;
    case ITEM_BOOK_BAR:
      return StrId::STR_BOOK_BAR;
    case ITEM_CHAPTER_BAR:
      return StrId::STR_CHAPTER_BAR;
    case ITEM_BAR_THICKNESS:
      return StrId::STR_BAR_THICKNESS;
    default:
      return StrId::STR_STATUS_BAR;
  }
}

// Anchor value (0..6) label. 0 = Off, 1..6 = TL,TC,TR,BL,BC,BR.
const StrId anchorNames[CrossPointSettings::STATUS_BAR_ANCHOR_COUNT] = {
    StrId::STR_STATE_OFF, StrId::STR_ANCHOR_TL, StrId::STR_ANCHOR_TC, StrId::STR_ANCHOR_TR,
    StrId::STR_ANCHOR_BL, StrId::STR_ANCHOR_BC, StrId::STR_ANCHOR_BR};

const StrId edgeNames[CrossPointSettings::STATUS_BAR_EDGE_COUNT] = {StrId::STR_STATE_OFF, StrId::STR_TOP,
                                                                    StrId::STR_BOTTOM};
const StrId thicknessNames[CrossPointSettings::STATUS_BAR_BAR_THICKNESS_COUNT] = {
    StrId::STR_SLIM, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_FAT};

// Row value for a position item: "[TC]" for an anchor, "Off" when parked.
std::string anchorRowValue(uint8_t v) {
  if (v == 0 || v >= CrossPointSettings::STATUS_BAR_ANCHOR_COUNT) return std::string(I18N.get(StrId::STR_STATE_OFF));
  return "[" + std::string(I18N.get(anchorNames[v])) + "]";
}

uint8_t cycle(uint8_t v, int count) { return static_cast<uint8_t>((v + 1) % count); }
}  // namespace

uint8_t* StatusBarSettingsActivity::anchorFieldFor(int itemId) const {
  switch (itemId) {
    case ITEM_BATTERY:
      return &SETTINGS.sbBatteryPos;
    case ITEM_CLOCK:
      return &SETTINGS.sbClockPos;
    case ITEM_TITLE:
      return &SETTINGS.sbTitlePos;
    case ITEM_PAGE:
      return &SETTINGS.sbPagePos;
    case ITEM_BOOK_PCT:
      return &SETTINGS.sbBookPctPos;
    case ITEM_CHAPTER_PCT:
      return &SETTINGS.sbChapterPctPos;
    case ITEM_CHAPTER_NUM:
      return &SETTINGS.sbChapterNumPos;
    default:
      return nullptr;
  }
}

void StatusBarSettingsActivity::onEnter() {
  Activity::onEnter();

  selectedIndex = 0;
  pickerActive = false;

  // Build the visible item list (clock only when the RTC is present).
  visibleItems.clear();
  for (int id = 0; id < ITEM_ID_COUNT; id++) {
    if (id == ITEM_CLOCK && !halClock.isAvailable()) continue;
    visibleItems.push_back(id);
  }

  // Clamp possibly-corrupt values so they index label arrays safely.
  auto clampField = [](uint8_t& f, int count) {
    if (f >= count) f = 0;
  };
  clampField(SETTINGS.sbBatteryPos, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
  clampField(SETTINGS.sbClockPos, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
  clampField(SETTINGS.sbTitlePos, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
  clampField(SETTINGS.sbPagePos, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
  clampField(SETTINGS.sbBookPctPos, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
  clampField(SETTINGS.sbChapterPctPos, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
  clampField(SETTINGS.sbChapterNumPos, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
  clampField(SETTINGS.sbTitleSource, CrossPointSettings::STATUS_BAR_TITLE_SOURCE_COUNT);
  clampField(SETTINGS.sbPageFormat, CrossPointSettings::STATUS_BAR_PAGE_FORMAT_COUNT);
  clampField(SETTINGS.sbBookBar, CrossPointSettings::STATUS_BAR_EDGE_COUNT);
  clampField(SETTINGS.sbChapterBar, CrossPointSettings::STATUS_BAR_EDGE_COUNT);
  clampField(SETTINGS.sbBarThickness, CrossPointSettings::STATUS_BAR_BAR_THICKNESS_COUNT);

  requestUpdate();
}

void StatusBarSettingsActivity::onExit() { Activity::onExit(); }

void StatusBarSettingsActivity::loop() {
  // --- Anchor picker overlay owns input while active ---
  if (pickerActive) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      pickerActive = false;
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (pickerTarget) *pickerTarget = static_cast<uint8_t>(pickerIndex);
      SETTINGS.saveToFile();
      pickerActive = false;
      requestUpdate();
      return;
    }
    buttonNavigator.onNextPress([this] {
      pickerIndex = ButtonNavigator::nextIndex(pickerIndex, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
      requestUpdate();
    });
    buttonNavigator.onPreviousPress([this] {
      pickerIndex = ButtonNavigator::previousIndex(pickerIndex, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
      requestUpdate();
    });
    buttonNavigator.onNextContinuous([this] {
      pickerIndex = ButtonNavigator::nextIndex(pickerIndex, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
      requestUpdate();
    });
    buttonNavigator.onPreviousContinuous([this] {
      pickerIndex = ButtonNavigator::previousIndex(pickerIndex, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
      requestUpdate();
    });
    return;
  }

  // --- Normal list navigation ---
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    requestUpdate();
    return;
  }

  const int count = static_cast<int>(visibleItems.size());
  buttonNavigator.onNextPress([this, count] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, count);
    requestUpdate();
  });
  buttonNavigator.onPreviousPress([this, count] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, count);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, count] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, count);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, count] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, count);
    requestUpdate();
  });
}

void StatusBarSettingsActivity::handleSelection() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(visibleItems.size())) return;
  const int id = visibleItems[selectedIndex];

  // Position items open the anchor picker.
  if (uint8_t* field = anchorFieldFor(id)) {
    pickerTarget = field;
    pickerIndex = *field;
    pickerActive = true;
    return;
  }

  switch (id) {
    case ITEM_ENABLED:
      SETTINGS.sbEnabled = cycle(SETTINGS.sbEnabled, 2);
      break;
    case ITEM_TITLE_SOURCE:
      SETTINGS.sbTitleSource = cycle(SETTINGS.sbTitleSource, CrossPointSettings::STATUS_BAR_TITLE_SOURCE_COUNT);
      break;
    case ITEM_TITLE_TRUNCATE:
      SETTINGS.sbTitleTruncate = cycle(SETTINGS.sbTitleTruncate, 2);
      break;
    case ITEM_PAGE_FORMAT:
      SETTINGS.sbPageFormat = cycle(SETTINGS.sbPageFormat, CrossPointSettings::STATUS_BAR_PAGE_FORMAT_COUNT);
      break;
    case ITEM_BOOK_BAR:
      SETTINGS.sbBookBar = cycle(SETTINGS.sbBookBar, CrossPointSettings::STATUS_BAR_EDGE_COUNT);
      break;
    case ITEM_CHAPTER_BAR:
      SETTINGS.sbChapterBar = cycle(SETTINGS.sbChapterBar, CrossPointSettings::STATUS_BAR_EDGE_COUNT);
      break;
    case ITEM_BAR_THICKNESS:
      SETTINGS.sbBarThickness = cycle(SETTINGS.sbBarThickness, CrossPointSettings::STATUS_BAR_BAR_THICKNESS_COUNT);
      break;
    default:
      return;
  }
  SETTINGS.saveToFile();
}

void StatusBarSettingsActivity::renderPicker() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int rowH = renderer.getLineHeight(UI_12_FONT_ID) + 6;
  const int rows = CrossPointSettings::STATUS_BAR_ANCHOR_COUNT;  // 7
  const int titleH = renderer.getLineHeight(UI_12_FONT_ID) + 8;
  const int boxW = 200;
  const int boxH = titleH + rows * rowH + 12;
  const int boxX = (screenW - boxW) / 2;
  const int boxY = (screenH - boxH) / 2;

  renderer.fillRect(boxX, boxY, boxW, boxH, false);  // white
  renderer.drawRect(boxX, boxY, boxW, boxH, 2, true);

  UITheme::drawCenteredText(renderer, Rect{boxX, boxY, boxW, boxH}, UI_12_FONT_ID, boxY + 6, tr(STR_POSITION), true,
                            EpdFontFamily::BOLD);

  int y = boxY + titleH + 4;
  for (int i = 0; i < rows; i++) {
    const bool sel = (i == pickerIndex);
    if (sel) renderer.fillRect(boxX + 4, y - 2, boxW - 8, rowH, true);
    UITheme::drawCenteredText(renderer, Rect{boxX, y, boxW, rowH}, UI_10_FONT_ID, y + 2, I18N.get(anchorNames[i]),
                              !sel);
    y += rowH;
  }
  (void)metrics;
}

void StatusBarSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CUSTOMISE_STATUS_BAR));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(visibleItems.size()), selectedIndex,
      [this](int index) { return std::string(I18N.get(itemLabel(visibleItems[index]))); }, nullptr, nullptr,
      [this](int index) -> std::string {
        const int id = visibleItems[index];
        if (const uint8_t* field = anchorFieldFor(id)) return anchorRowValue(*field);
        switch (id) {
          case ITEM_ENABLED:
            return SETTINGS.sbEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
          case ITEM_TITLE_SOURCE:
            return SETTINGS.sbTitleSource == CrossPointSettings::SB_TITLE_CHAPTER ? tr(STR_CHAPTER) : tr(STR_BOOK);
          case ITEM_TITLE_TRUNCATE:
            return SETTINGS.sbTitleTruncate ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
          case ITEM_PAGE_FORMAT:
            return SETTINGS.sbPageFormat == CrossPointSettings::SB_PAGE_LEFT ? tr(STR_PAGE_LEFT)
                                                                             : tr(STR_PAGE_FRACTION);
          case ITEM_BOOK_BAR:
            return I18N.get(
                edgeNames[SETTINGS.sbBookBar < CrossPointSettings::STATUS_BAR_EDGE_COUNT ? SETTINGS.sbBookBar : 0]);
          case ITEM_CHAPTER_BAR:
            return I18N.get(
                edgeNames[SETTINGS.sbChapterBar < CrossPointSettings::STATUS_BAR_EDGE_COUNT ? SETTINGS.sbChapterBar
                                                                                            : 0]);
          case ITEM_BAR_THICKNESS:
            return I18N.get(thicknessNames[SETTINGS.sbBarThickness < CrossPointSettings::STATUS_BAR_BAR_THICKNESS_COUNT
                                               ? SETTINGS.sbBarThickness
                                               : 0]);
          default:
            return "";
        }
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (pickerActive) renderPicker();

  // Periodic full refresh to neutralise X3 fast-refresh bloom (see header).
  const bool full = (renderCount % kFullRefreshEvery == 0);
  renderCount++;
  renderer.present(full ? RefreshIntent::DeepClean : RefreshIntent::MenuNav);
}
