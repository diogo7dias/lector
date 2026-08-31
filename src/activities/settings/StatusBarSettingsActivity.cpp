#include "StatusBarSettingsActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>

#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
// Item ids in display order. Clock is X3-only and is filtered out in onEnter().
enum ItemId {
  ITEM_ENABLED,         // master on/off (toggle)
  ITEM_OFF_BAR,         // Off / Slim / Medium / Fat (cycle) — only while ENABLED is off
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
  ITEM_SESSION_PAGES,   // anchor
  ITEM_PARA_PAGES,      // anchor
  ITEM_BOOK_BAR,        // Off / Top / Bottom (cycle)
  ITEM_CHAPTER_BAR,     // Off / Top / Bottom (cycle)
  ITEM_BAR_THICKNESS,   // Slim / Medium / Fat (cycle)
  ITEM_FLOATING_BAR,    // On / Off (toggle)
  ITEM_BAR_OUTLINE,     // On / Off (toggle)
  ITEM_ID_COUNT
};

StrId itemLabel(int id) {
  switch (id) {
    case ITEM_ENABLED:
      return StrId::STR_STATUS_BAR;
    case ITEM_OFF_BAR:
      return StrId::STR_PROGRESS_BAR;
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
    case ITEM_SESSION_PAGES:
      return StrId::STR_SESSION_PAGES;
    case ITEM_PARA_PAGES:
      return StrId::STR_PARA_PAGES;
    case ITEM_BOOK_BAR:
      return StrId::STR_BOOK_BAR;
    case ITEM_CHAPTER_BAR:
      return StrId::STR_CHAPTER_BAR;
    case ITEM_BAR_THICKNESS:
      return StrId::STR_BAR_THICKNESS;
    case ITEM_FLOATING_BAR:
      return StrId::STR_FLOATING_BAR;
    case ITEM_BAR_OUTLINE:
      return StrId::STR_BAR_OUTLINE;
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
// Off plus the same three thicknesses, so the row is both the switch and the size.
const StrId offBarNames[CrossPointSettings::STATUS_BAR_OFF_BAR_COUNT] = {
    StrId::STR_STATE_OFF, StrId::STR_SLIM, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_FAT};

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
    case ITEM_SESSION_PAGES:
      return &SETTINGS.sbSessionPagesPos;
    case ITEM_PARA_PAGES:
      return &SETTINGS.sbParaPagesPos;
    default:
      return nullptr;
  }
}

void StatusBarSettingsActivity::rebuildVisibleItems() {
  visibleItems.clear();
  for (int id = 0; id < ITEM_ID_COUNT; id++) {
    if (id == ITEM_CLOCK && !halClock.isAvailable()) continue;
    // The hidden-bar progress row is dead weight while the status bar is on: the bar
    // already draws from Book Bar / Chapter Bar + Bar Thickness there.
    if (id == ITEM_OFF_BAR && SETTINGS.sbEnabled) continue;
    visibleItems.push_back(id);
  }
}

void StatusBarSettingsActivity::onEnter() {
  rebuildVisibleItems();

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
  clampField(SETTINGS.sbSessionPagesPos, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
  clampField(SETTINGS.sbParaPagesPos, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
  clampField(SETTINGS.sbTitleSource, CrossPointSettings::STATUS_BAR_TITLE_SOURCE_COUNT);
  clampField(SETTINGS.sbPageFormat, CrossPointSettings::STATUS_BAR_PAGE_FORMAT_COUNT);
  clampField(SETTINGS.sbBookBar, CrossPointSettings::STATUS_BAR_EDGE_COUNT);
  clampField(SETTINGS.sbChapterBar, CrossPointSettings::STATUS_BAR_EDGE_COUNT);
  clampField(SETTINGS.sbBarThickness, CrossPointSettings::STATUS_BAR_BAR_THICKNESS_COUNT);
  clampField(SETTINGS.sbOffBar, CrossPointSettings::STATUS_BAR_OFF_BAR_COUNT);

  // Last: it resets the selection and asks for the first paint, so the rows and the
  // clamped values must already be settled.
  UiListActivity::onEnter();
}

void StatusBarSettingsActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
  subtitles.clear();
}

bool StatusBarSettingsActivity::handleCustomInput() {
  // The anchor picker is a modal over the list: while it is up it takes every
  // button, and the list underneath sees none of them.
  return anchorPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void StatusBarSettingsActivity::handleSelection(const int index) {
  if (index < 0 || index >= static_cast<int>(visibleItems.size())) return;
  const int id = visibleItems[index];

  // Position items open the anchor picker.
  if (uint8_t* field = anchorFieldFor(id)) {
    anchorPopup.show(StrId::STR_POSITION, anchorNames, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT, *field,
                     [this, field](const int choice) {
                       if (choice >= 0 && choice < CrossPointSettings::STATUS_BAR_ANCHOR_COUNT) {
                         *field = static_cast<uint8_t>(choice);
                         SETTINGS.saveToFile();
                       }
                       requestUpdate();
                     });
    return;
  }

  switch (id) {
    case ITEM_ENABLED:
      SETTINGS.sbEnabled = cycle(SETTINGS.sbEnabled, 2);
      // Turning the bar off reveals the hidden-bar progress row directly below this
      // one; turning it back on hides it again. Rebuild before the selection can point
      // past the shortened list. This row is index 0, so the cursor stays put.
      rebuildVisibleItems();
      break;
    case ITEM_OFF_BAR:
      SETTINGS.sbOffBar = cycle(SETTINGS.sbOffBar, CrossPointSettings::STATUS_BAR_OFF_BAR_COUNT);
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
    case ITEM_FLOATING_BAR:
      SETTINGS.sbFloatingBar = cycle(SETTINGS.sbFloatingBar, 2);
      break;
    case ITEM_BAR_OUTLINE:
      SETTINGS.sbBarOutline = cycle(SETTINGS.sbBarOutline, 2);
      break;
    default:
      return;
  }
  SETTINGS.saveToFile();
}

void StatusBarSettingsActivity::activateIndex(const int index) {
  app.clearTapFlash();
  handleSelection(index);
  requestUpdate();
}

// The value shown on the right of a row: an anchor in brackets for a position item,
// otherwise whatever that row cycles through. Pulled out of the old drawList callback
// unchanged, so the rows read exactly as they did.
std::string StatusBarSettingsActivity::rowValue(const int id) const {
  if (const uint8_t* field = anchorFieldFor(id)) return anchorRowValue(*field);
  switch (id) {
    case ITEM_ENABLED:
      return SETTINGS.sbEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case ITEM_OFF_BAR:
      return I18N.get(
          offBarNames[SETTINGS.sbOffBar < CrossPointSettings::STATUS_BAR_OFF_BAR_COUNT ? SETTINGS.sbOffBar : 0]);
    case ITEM_TITLE_SOURCE:
      return SETTINGS.sbTitleSource == CrossPointSettings::SB_TITLE_CHAPTER ? tr(STR_CHAPTER) : tr(STR_BOOK);
    case ITEM_TITLE_TRUNCATE:
      return SETTINGS.sbTitleTruncate ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case ITEM_PAGE_FORMAT:
      return SETTINGS.sbPageFormat == CrossPointSettings::SB_PAGE_LEFT ? tr(STR_PAGE_LEFT) : tr(STR_PAGE_FRACTION);
    case ITEM_BOOK_BAR:
      return I18N.get(edgeNames[SETTINGS.sbBookBar < CrossPointSettings::STATUS_BAR_EDGE_COUNT ? SETTINGS.sbBookBar
                                                                                              : 0]);
    case ITEM_CHAPTER_BAR:
      return I18N.get(
          edgeNames[SETTINGS.sbChapterBar < CrossPointSettings::STATUS_BAR_EDGE_COUNT ? SETTINGS.sbChapterBar : 0]);
    case ITEM_BAR_THICKNESS:
      return I18N.get(thicknessNames[SETTINGS.sbBarThickness < CrossPointSettings::STATUS_BAR_BAR_THICKNESS_COUNT
                                         ? SETTINGS.sbBarThickness
                                         : 0]);
    case ITEM_FLOATING_BAR:
      return SETTINGS.sbFloatingBar ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case ITEM_BAR_OUTLINE:
      return SETTINGS.sbBarOutline ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    default:
      return std::string();
  }
}

void StatusBarSettingsActivity::buildScreen(UiScreen& screen) {
  const int itemCount = static_cast<int>(visibleItems.size());
  subtitles.assign(static_cast<size_t>(itemCount), std::string());
  rows.assign(static_cast<size_t>(itemCount), fui::ListItem{});

  for (int i = 0; i < itemCount; ++i) {
    const int id = visibleItems[i];
    rows[i].label = I18N.get(itemLabel(id));
    subtitles[i] = rowValue(id);
    if (!subtitles[i].empty()) rows[i].subtitle = subtitles[i].c_str();
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(itemCount);
  props.action = ACTION_ROW;
  syncListViewport(screen, props, true);
  screen.list(props);
}

bool StatusBarSettingsActivity::drawOverlay() { return anchorPopup.processRender(renderer, mappedInput); }
