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

uint8_t* StatusBarSettingsActivity::anchorFieldFor(int itemId) {
  switch (itemId) {
    case ITEM_BATTERY:
      return &sb.batteryPos;
    case ITEM_CLOCK:
      return &sb.clockPos;
    case ITEM_TITLE:
      return &sb.titlePos;
    case ITEM_PAGE:
      return &sb.pagePos;
    case ITEM_BOOK_PCT:
      return &sb.bookPctPos;
    case ITEM_CHAPTER_PCT:
      return &sb.chapterPctPos;
    case ITEM_CHAPTER_NUM:
      return &sb.chapterNumPos;
    case ITEM_SESSION_PAGES:
      return &sb.sessionPagesPos;
    case ITEM_PARA_PAGES:
      return &sb.paraPagesPos;
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
    if (id == ITEM_OFF_BAR && sb.enabled) continue;
    visibleItems.push_back(id);
  }
}

void StatusBarSettingsActivity::onEnter() {
  rebuildVisibleItems();

  // Clamp possibly-corrupt values so they index label arrays safely.
  auto clampField = [](uint8_t& f, int count) {
    if (f >= count) f = 0;
  };
  clampField(sb.batteryPos, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
  clampField(sb.clockPos, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
  clampField(sb.titlePos, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
  clampField(sb.pagePos, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
  clampField(sb.bookPctPos, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
  clampField(sb.chapterPctPos, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
  clampField(sb.chapterNumPos, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
  clampField(sb.sessionPagesPos, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
  clampField(sb.paraPagesPos, CrossPointSettings::STATUS_BAR_ANCHOR_COUNT);
  clampField(sb.titleSource, CrossPointSettings::STATUS_BAR_TITLE_SOURCE_COUNT);
  clampField(sb.pageFormat, CrossPointSettings::STATUS_BAR_PAGE_FORMAT_COUNT);
  clampField(sb.bookBar, CrossPointSettings::STATUS_BAR_EDGE_COUNT);
  clampField(sb.chapterBar, CrossPointSettings::STATUS_BAR_EDGE_COUNT);
  clampField(sb.barThickness, CrossPointSettings::STATUS_BAR_BAR_THICKNESS_COUNT);
  clampField(sb.offBar, CrossPointSettings::STATUS_BAR_OFF_BAR_COUNT);

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
                         sink(sinkCtx, sb);
                       }
                       requestUpdate();
                     });
    return;
  }

  switch (id) {
    case ITEM_ENABLED:
      sb.enabled = cycle(sb.enabled, 2);
      // Turning the bar off reveals the hidden-bar progress row directly below this
      // one; turning it back on hides it again. Rebuild before the selection can point
      // past the shortened list. This row is index 0, so the cursor stays put.
      rebuildVisibleItems();
      break;
    case ITEM_OFF_BAR:
      sb.offBar = cycle(sb.offBar, CrossPointSettings::STATUS_BAR_OFF_BAR_COUNT);
      break;
    case ITEM_TITLE_SOURCE:
      sb.titleSource = cycle(sb.titleSource, CrossPointSettings::STATUS_BAR_TITLE_SOURCE_COUNT);
      break;
    case ITEM_TITLE_TRUNCATE:
      sb.titleTruncate = cycle(sb.titleTruncate, 2);
      break;
    case ITEM_PAGE_FORMAT:
      sb.pageFormat = cycle(sb.pageFormat, CrossPointSettings::STATUS_BAR_PAGE_FORMAT_COUNT);
      break;
    case ITEM_BOOK_BAR:
      sb.bookBar = cycle(sb.bookBar, CrossPointSettings::STATUS_BAR_EDGE_COUNT);
      break;
    case ITEM_CHAPTER_BAR:
      sb.chapterBar = cycle(sb.chapterBar, CrossPointSettings::STATUS_BAR_EDGE_COUNT);
      break;
    case ITEM_BAR_THICKNESS:
      sb.barThickness = cycle(sb.barThickness, CrossPointSettings::STATUS_BAR_BAR_THICKNESS_COUNT);
      break;
    case ITEM_FLOATING_BAR:
      sb.floatingBar = cycle(sb.floatingBar, 2);
      break;
    case ITEM_BAR_OUTLINE:
      sb.barOutline = cycle(sb.barOutline, 2);
      break;
    default:
      return;
  }
  sink(sinkCtx, sb);
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
  // anchorFieldFor() only hands out a pointer into sb; nothing is written through it here.
  if (const uint8_t* field = const_cast<StatusBarSettingsActivity*>(this)->anchorFieldFor(id)) {
    return anchorRowValue(*field);
  }
  switch (id) {
    case ITEM_ENABLED:
      return sb.enabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case ITEM_OFF_BAR:
      return I18N.get(offBarNames[sb.offBar < CrossPointSettings::STATUS_BAR_OFF_BAR_COUNT ? sb.offBar : 0]);
    case ITEM_TITLE_SOURCE:
      return sb.titleSource == CrossPointSettings::SB_TITLE_CHAPTER ? tr(STR_CHAPTER) : tr(STR_BOOK);
    case ITEM_TITLE_TRUNCATE:
      return sb.titleTruncate ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case ITEM_PAGE_FORMAT:
      return sb.pageFormat == CrossPointSettings::SB_PAGE_LEFT ? tr(STR_PAGE_LEFT) : tr(STR_PAGE_FRACTION);
    case ITEM_BOOK_BAR:
      return I18N.get(edgeNames[sb.bookBar < CrossPointSettings::STATUS_BAR_EDGE_COUNT ? sb.bookBar : 0]);
    case ITEM_CHAPTER_BAR:
      return I18N.get(edgeNames[sb.chapterBar < CrossPointSettings::STATUS_BAR_EDGE_COUNT ? sb.chapterBar : 0]);
    case ITEM_BAR_THICKNESS:
      return I18N.get(
          thicknessNames[sb.barThickness < CrossPointSettings::STATUS_BAR_BAR_THICKNESS_COUNT ? sb.barThickness : 0]);
    case ITEM_FLOATING_BAR:
      return sb.floatingBar ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case ITEM_BAR_OUTLINE:
      return sb.barOutline ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
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
