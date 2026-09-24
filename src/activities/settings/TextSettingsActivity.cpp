#include "TextSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderFontSizes.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "TextSettingsPreview.h"
#include "activities/settings/FontPickerActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "components/RowHitTest.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/MarginLink.h"

namespace {
// Look fields by their CrossPointSettings member, so a row finds the ReaderPrefs byte
// behind its SettingInfo entry. Generated from the field lists: the names are identical on
// both structs, which is what lets one list serve both sides.
struct LookField {
  uint8_t CrossPointSettings::* setting;
  uint8_t ReaderPrefs::* look;
};
constexpr LookField LOOK_FIELDS[] = {
#define CP_LOOK_FIELD(name) {&CrossPointSettings::name, &ReaderPrefs::name},
    // cppcheck-suppress unknownMacro
    READER_LOOK_SCREEN_FIELDS(CP_LOOK_FIELD) READER_LOOK_BOOK_FIELDS(CP_LOOK_FIELD)
#undef CP_LOOK_FIELD
};

// The two options that defer to the book's own CSS, and so go inert when Embedded Layout
// Style is off.
constexpr uint8_t ALIGNMENT_BOOK_INDEX = 4;    // STR_BOOK_S_STYLE
constexpr uint8_t INDENT_MODE_BOOK_INDEX = 0;  // STR_INDENT_BOOK

std::string needsLayoutLabel(const std::string& label) {
  return label + " (" + I18N.get(StrId::STR_NEEDS_EMBEDDED_LAYOUT) + ")";
}

// The preview is the whole point of the screen, so it takes a fixed slice of the height
// rather than a share that moves with the row under focus: a pane that resized as
// navigation walked the list would repaint the entire panel on every step. 320px fits both
// page ends plus their margins at the panel's own type size; the cap keeps a shorter
// viewport (landscape, or a smaller panel) from leaving no room for the list.
constexpr int PREVIEW_HEIGHT = 320;
constexpr int PREVIEW_MAX_PERCENT = 55;

int findCurrentFontIndex(const SdCardFontRegistry* registry, const char* sdFontFamilyName, uint8_t fontFamily) {
  if (sdFontFamilyName[0] != '\0' && registry) {
    const auto& families = registry->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      if (families[i].name == sdFontFamilyName) {
        return CrossPointSettings::BUILTIN_FONT_COUNT + i;
      }
    }
  }

  return fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? fontFamily : 0;
}
}  // namespace

TextSettingsActivity::TextSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const SdCardFontRegistry* registry, const ReaderPrefs& start,
                                           const Sink sink, void* sinkCtx)
    : UiGridActivity("TextSettings", renderer, mappedInput),
      registry_(registry),
      look_(start),
      sink_(sink),
      sinkCtx_(sinkCtx) {}

void TextSettingsActivity::onEnter() {
  UiGridActivity::onEnter();

  metrics_ = UITheme::getInstance().getMetrics();
  afterHeader = metrics_.topPadding + metrics_.headerHeight + metrics_.verticalSpacing;
  bottomReserved = metrics_.buttonHintsHeight + metrics_.verticalSpacing;
  usableHeight = renderer.getScreenHeight() - afterHeader - bottomReserved;

  fonts_.clear();
  fonts_.reserve(CrossPointSettings::BUILTIN_FONT_COUNT + (registry_ ? registry_->getFamilyCount() : 0));
  fonts_.push_back({I18N.get(StrId::STR_CHAREINK), true, static_cast<uint8_t>(CrossPointSettings::CHAREINK)});
  if (registry_) {
    const auto& families = registry_->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      fonts_.push_back({families[i].name, false, static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i)});
    }
  }

  rebuildSizeList();
  currentFamilyIndex_ = findCurrentFontIndex(registry_, look_.sdFontFamilyName, look_.fontFamily);
  setSelected(0);

  requestUpdate();
}

void TextSettingsActivity::onExit() {
  commitSettings();
  Activity::onExit();
}

// The selectable sizes belong to the active family, so this runs on entry and again after
// every family change. A family change goes through loadLookFont(), which snaps
// look_.fontPointSize into the new family's set — but entry does not, so the highlight
// is resolved by snapping rather than by exact match.
void TextSettingsActivity::rebuildSizeList() {
  const std::vector<uint8_t> points = readerFontPointSizes(registry_, look_.sdFontFamilyName);

  // The stored size can still sit outside this family's set — e.g. the family was deleted
  // while selected, or the card was swapped. Highlight the size the reader actually
  // renders, which getReaderFontId() resolves the same way.
  const uint8_t selectedPt = snapToNearestPointSize(points, look_.fontPointSize);

  sizes_.clear();
  sizes_.reserve(points.size());
  currentSizeIndex_ = 0;
  for (const uint8_t pt : points) {
    // "pt" is deliberately not translated: it is the typographic unit symbol, written the
    // same way in every language CrossPoint ships.
    char label[12];
    snprintf(label, sizeof(label), "%u pt", pt);
    if (pt == selectedPt) currentSizeIndex_ = static_cast<int>(sizes_.size());
    sizes_.push_back({label, pt});
  }
}

// The preview band above the grid; the base reserves it and hands its rect back.
int TextSettingsActivity::reservedHeight() const {
  return std::min(PREVIEW_HEIGHT, usableHeight * PREVIEW_MAX_PERCENT / 100) + metrics_.verticalSpacing;
}

void TextSettingsActivity::drawReserved(const Rect& rect) {
  // The preview is a real page rendered by the reader engine, so it stays a raw
  // painter; the base only decides where it goes.
  textsettings::renderPreview(renderer, previewLayout_, look_, rect.y, rect.height - metrics_.verticalSpacing);
}

namespace {
// Each cell's SettingInfo entry, by the CrossPointSettings field it edits. Everything else
// about the cell (name, kind, range, step, option labels) is read from that entry, so a
// look setting added to SettingsList.h needs only a Row, a line here and a place in
// visibleRows(). The grid shortens a few names, and Word Spacing moves by its full step
// on Up/Down as well as on the large step.
struct RowSpec {
  uint8_t CrossPointSettings::* setting;  // nullptr: Font and Size, fed by the font registry
  StrId shortName;                        // STR_NONE_OPT: the entry's own name
  bool coarseSteps;
};
constexpr RowSpec ROW_SPECS[] = {
    {nullptr, StrId::STR_FONT, false},                                          // Font
    {nullptr, StrId::STR_SIZE, false},                                          // Size
    {&CrossPointSettings::paperbackLookBody, StrId::STR_NONE_OPT, false},       // PaperbackLook
    {&CrossPointSettings::lineSpacingPercent, StrId::STR_NONE_OPT, false},      // LineSpacing
    {&CrossPointSettings::extraParagraphSpacing, StrId::STR_NONE_OPT, false},   // ExtraSpacing
    {&CrossPointSettings::wordSpacing, StrId::STR_NONE_OPT, true},              // WordSpacing
    {&CrossPointSettings::paragraphAlignment, StrId::STR_ALIGNMENT, false},     // Alignment
    {&CrossPointSettings::firstLineIndentMode, StrId::STR_NONE_OPT, false},     // IndentMode
    {&CrossPointSettings::firstLineIndentPercent, StrId::STR_NONE_OPT, false},  // IndentPercent
    {&CrossPointSettings::screenMargin, StrId::STR_NONE_OPT, false},            // HorizontalMargin
    {&CrossPointSettings::marginLinkMode, StrId::STR_NONE_OPT, false},          // MarginLink
    {&CrossPointSettings::screenMarginTop, StrId::STR_VERTICAL_MARGIN, false},  // VerticalMargin
    {&CrossPointSettings::screenMarginTop, StrId::STR_NONE_OPT, false},         // TopMargin
    {&CrossPointSettings::screenMarginBottom, StrId::STR_NONE_OPT, false},      // BottomMargin
    {&CrossPointSettings::dynamicMargins, StrId::STR_NONE_OPT, false},          // DynamicMargins
    {&CrossPointSettings::focusReadingEnabled, StrId::STR_NONE_OPT, false},     // FocusReading
    {&CrossPointSettings::guideDotsEnabled, StrId::STR_NONE_OPT, false},        // GuideDots
    {&CrossPointSettings::guideDotsHidden, StrId::STR_NONE_OPT, false},         // HiddenDots
    {&CrossPointSettings::embeddedTextStyle, StrId::STR_NONE_OPT, false},       // EmbeddedTextStyle
    {&CrossPointSettings::embeddedLayoutStyle, StrId::STR_NONE_OPT, false},     // EmbeddedLayoutStyle
    {&CrossPointSettings::textAntiAliasing, StrId::STR_NONE_OPT, false},        // AntiAliasing
    {&CrossPointSettings::debugBorders, StrId::STR_NONE_OPT, false},            // DebugBorders
};
}  // namespace

const SettingInfo* TextSettingsActivity::settingOf(const Row row) {
  static_assert(std::size(ROW_SPECS) == static_cast<size_t>(Row::Count),
                "every Row needs exactly one ROW_SPECS line, in Row order");
  const auto member = ROW_SPECS[static_cast<size_t>(row)].setting;
  if (!member) return nullptr;
  // The static list itself, not getSettingsList(): that copies all ~90 entries.
  for (const SettingInfo& info : settingsBaseList()) {
    if (info.valuePtr == member) return &info;
  }
  return nullptr;
}

TextSettingsActivity::RowKind TextSettingsActivity::kindOf(const Row row) {
  if (row == Row::Font) return RowKind::FontList;
  if (row == Row::Size) return RowKind::Picker;
  const SettingInfo* info = settingOf(row);
  if (!info) return RowKind::Toggle;
  switch (info->type) {
    case SettingType::ENUM:
      return RowKind::Picker;
    case SettingType::VALUE:
      return RowKind::Number;
    default:
      return RowKind::Toggle;
  }
}

std::vector<TextSettingsActivity::Row> TextSettingsActivity::visibleRows() const {
  // Grid order, two cells to a row, so consecutive pairs are settings you judge together.
  // The section headings the list used to carry are gone: a cell shows its own name, and
  // four bands would have cost two grid rows to say what the pairing already says.
  std::vector<Row> rows;
  rows.reserve(25);

  rows.push_back(Row::Font);
  rows.push_back(Row::Size);

  // The two that change how the ink itself sits on the page.
  rows.push_back(Row::PaperbackLook);
  rows.push_back(Row::AntiAliasing);

  rows.push_back(Row::LineSpacing);
  rows.push_back(Row::ExtraSpacing);
  rows.push_back(Row::WordSpacing);

  rows.push_back(Row::Alignment);
  rows.push_back(Row::IndentMode);
  // The custom-% value only applies in Custom% mode; in Book mode the indent comes from
  // the EPUB's own CSS, so there is nothing to tune.
  if (look_.firstLineIndentMode == CrossPointSettings::FIRST_LINE_INDENT_PERCENT) {
    rows.push_back(Row::IndentPercent);
  }

  // All Sides: the horizontal cell is every side, so it is the only margin cell, and
  // Dynamic Margins is not offered at all — it would compute a horizontal margin of its
  // own and leave left/right disagreeing with top/bottom.
  rows.push_back(Row::HorizontalMargin);
  rows.push_back(Row::MarginLink);
  switch (margin_link::toMode(look_.marginLinkMode)) {
    case margin_link::Mode::AllSides:
      break;
    case margin_link::Mode::TopBottom:
      rows.push_back(Row::VerticalMargin);
      rows.push_back(Row::DynamicMargins);
      break;
    case margin_link::Mode::Separate:
      rows.push_back(Row::TopMargin);
      rows.push_back(Row::BottomMargin);
      rows.push_back(Row::DynamicMargins);
      break;
  }

  rows.push_back(Row::FocusReading);

  rows.push_back(Row::GuideDots);
  // Hidden Dots only says anything about a page that is already drawing guide dots.
  if (look_.guideDotsEnabled) rows.push_back(Row::HiddenDots);

  rows.push_back(Row::EmbeddedTextStyle);
  rows.push_back(Row::EmbeddedLayoutStyle);

  rows.push_back(Row::DebugBorders);
  return rows;
}

StrId TextSettingsActivity::rowNameId(const Row row) const {
  // In All Sides this row is the only margin there is, so naming it "Horizontal" would be
  // describing a side rather than what it does.
  if (row == Row::HorizontalMargin && margin_link::toMode(look_.marginLinkMode) == margin_link::Mode::AllSides) {
    return StrId::STR_MARGIN;
  }
  const StrId shortName = ROW_SPECS[static_cast<size_t>(row)].shortName;
  if (shortName != StrId::STR_NONE_OPT) return shortName;
  const SettingInfo* info = settingOf(row);
  return info ? info->nameId : StrId::STR_NONE_OPT;
}

// The vertical margins are two stored fields even while they are linked, so the linked row
// edits the top one and applyNumber() mirrors it into the bottom.
uint8_t* TextSettingsActivity::lookField(const Row row) {
  const auto member = ROW_SPECS[static_cast<size_t>(row)].setting;
  if (!member) return nullptr;
  for (const LookField& field : LOOK_FIELDS) {
    if (field.setting == member) return &(look_.*(field.look));
  }
  return nullptr;
}

void TextSettingsActivity::applyNumber(const Row row, const int value) {
  uint8_t* field = lookField(row);
  if (!field) return;
  const margin_link::Margins current{look_.screenMargin, look_.screenMarginTop, look_.screenMarginBottom};
  const margin_link::Mode mode = margin_link::toMode(look_.marginLinkMode);
  const auto write = [this](const margin_link::Margins next) {
    look_.screenMargin = next.horizontal;
    look_.screenMarginTop = next.top;
    look_.screenMarginBottom = next.bottom;
  };
  switch (row) {
    // Every margin row writes through the same rule the migration and the mode picker
    // use, so a row that stands for more than one side carries all of them.
    case Row::HorizontalMargin:
      write(margin_link::setHorizontal(current, static_cast<uint8_t>(value), mode));
      break;
    case Row::VerticalMargin:
    case Row::TopMargin:
      write(margin_link::setTop(current, static_cast<uint8_t>(value), mode));
      break;
    case Row::BottomMargin:
      write(margin_link::setBottom(current, static_cast<uint8_t>(value), mode));
      break;
    default:
      *field = static_cast<uint8_t>(value);
      break;
  }
  settingsDirty_ = true;
}

// Write the settings file if an edited value is still waiting to be persisted. Called
// when a value dialog closes, when the row is left, and on the way out of the screen,
// so powering off or sleeping from inside Text Settings cannot lose the change
// (the same failure applySize() guards against, upstream #2806).
void TextSettingsActivity::commitSettings() {
  if (!settingsDirty_) return;
  settingsDirty_ = false;
  commit();
}

void TextSettingsActivity::commit() { sink_(sinkCtx_, look_); }

// Makes the resident SD font the one look_ names. A family change can leave the size
// outside the new family's set, so it is snapped in first: the reader resolves the
// resident size whatever size is stored, and the stored one has to say the same.
void TextSettingsActivity::loadLookFont() {
  look_.fontPointSize =
      snapToNearestPointSize(readerFontPointSizes(registry_, look_.sdFontFamilyName), look_.fontPointSize);
  sdFontSystem.ensureLoadedFor(renderer, look_.sdFontFamilyName, look_.fontPointSize);
}

std::string TextSettingsActivity::rowValueText(const Row row) const {
  const auto onOff = [](bool on) { return on ? tr(STR_STATE_ON) : tr(STR_STATE_OFF); };
  switch (row) {
    case Row::Font:
      return (currentFamilyIndex_ >= 0 && currentFamilyIndex_ < static_cast<int>(fonts_.size()))
                 ? fonts_[currentFamilyIndex_].name
                 : "";
    case Row::Size:
      return (currentSizeIndex_ >= 0 && currentSizeIndex_ < static_cast<int>(sizes_.size()))
                 ? sizes_[currentSizeIndex_].name
                 : "";
    case Row::DebugBorders:
      return onOff(SETTINGS.debugBorders);
    default:
      break;
  }
  const uint8_t* field = lookField(row);
  const SettingInfo* info = settingOf(row);
  if (!field || !info) return "";
  switch (kindOf(row)) {
    case RowKind::Toggle:
      return onOff(*field);
    case RowKind::Number:
      return std::to_string(*field);
    default:
      break;
  }
  const uint8_t v = *field;
  if (info->enumValues.empty()) return "";
  const std::string label = I18N.get(v < info->enumValues.size() ? info->enumValues[v] : info->enumValues[0]);
  // "Book's Style" and First Line Indent: Book read the book's own CSS, which is exactly
  // what Embedded Layout Style switches off. Saying so on the row beats a setting that
  // looks chosen and does nothing.
  const bool defersToBook =
      (row == Row::Alignment && v == ALIGNMENT_BOOK_INDEX) || (row == Row::IndentMode && v == INDENT_MODE_BOOK_INDEX);
  return (defersToBook && !look_.embeddedLayoutStyle) ? needsLayoutLabel(label) : label;
}

// Up and Down move a whole grid row so the column is kept; Left and Right move one cell,
// which is what makes the second column reachable. Clamped rather than wrapped: a wrap at
// the end of a settings screen reads as a jump rather than as a step.
int TextSettingsActivity::cellCount() const { return static_cast<int>(visibleRows().size()); }

const char* TextSettingsActivity::cellName(const int index) const {
  const auto rows = visibleRows();
  if (index < 0 || index >= static_cast<int>(rows.size())) return nullptr;
  cellNameScratch_ = I18N.get(rowNameId(rows[index]));
  return cellNameScratch_.c_str();
}

const char* TextSettingsActivity::cellValue(const int index) const {
  const auto rows = visibleRows();
  if (index < 0 || index >= static_cast<int>(rows.size())) return nullptr;
  cellValueScratch_ = rowValueText(rows[index]);
  return cellValueScratch_.c_str();
}

void TextSettingsActivity::activateCell(const int index) {
  const auto rows = visibleRows();
  if (index < 0 || index >= static_cast<int>(rows.size())) return;
  activateRow(rows[index]);
  requestUpdate();
}

void TextSettingsActivity::openSizePicker() {
  std::vector<std::string> options;
  options.reserve(sizes_.size());
  for (const auto& size : sizes_) options.push_back(size.name);
  optionPopup_.show(StrId::STR_SIZE, options, currentSizeIndex_, [this](int index) {
    if (index != currentSizeIndex_) applySize(index);  // applySize() persists
  });
}

void TextSettingsActivity::activateRow(const Row row) {
  switch (kindOf(row)) {
    case RowKind::FontList: {
      std::vector<std::string> names;
      names.reserve(fonts_.size());
      for (const auto& font : fonts_) names.push_back(font.name);
      startActivityForResult(
          std::make_unique<FontPickerActivity>(renderer, mappedInput, std::move(names), currentFamilyIndex_),
          [this](const ActivityResult& result) {
            const auto* chosen = std::get_if<IntervalResult>(&result.data);
            // applyFamily persists on every path out of itself: relying on a
            // later save loses the change when this screen is left by the home
            // key or by a sleep.
            if (chosen != nullptr && static_cast<int>(chosen->value) != currentFamilyIndex_) {
              applyFamily(static_cast<int>(chosen->value));
            }
            requestUpdate();
          });
      requestUpdate();
      return;
    }
    case RowKind::Number: {
      const SettingInfo* info = settingOf(row);
      const uint8_t* field = lookField(row);
      if (!info || !field) return;
      const SettingInfo::ValueRange range = info->valueRange;
      // A dedicated slider screen rather than a band over this list. The live
      // preview under the band is lost, but the row is only two taps away and
      // the number gets a finger-sized track instead of a header's worth of it.
      auto dialog = makeUniqueNoThrow<IntervalSelectionActivity>(
          renderer, mappedInput, "TextSettingNumber", rowNameId(row), *field, range.min, range.max,
          /*smallStep=*/ROW_SPECS[static_cast<size_t>(row)].coarseSteps ? range.step : 1, /*largeStep=*/range.step);
      if (!dialog) {
        LOG_ERR("TXTSET", "OOM: IntervalSelectionActivity");
        return;
      }
      startActivityForResult(std::move(dialog), [this, row](const ActivityResult& result) {
        const auto* chosen = std::get_if<IntervalResult>(&result.data);
        if (!result.isCancelled && chosen != nullptr) {
          setEditedValue(row, static_cast<int>(chosen->value));
          commitSettings();
        }
        requestUpdate();
      });
      requestUpdate();
      return;
    }
    case RowKind::Picker: {
      if (row == Row::Size) {
        openSizePicker();
        requestUpdate();
        return;
      }
      const SettingInfo* info = settingOf(row);
      uint8_t* field = lookField(row);
      if (!info || !field) return;
      const StrId* labels = info->enumValues.data();
      const int labelCount = static_cast<int>(info->enumValues.size());
      if (row == Row::MarginLink) {
        optionPopup_.show(rowNameId(row), labels, labelCount, look_.marginLinkMode, [this](int idx) {
          const auto next = static_cast<uint8_t>(idx);
          if (next == look_.marginLinkMode) return;  // re-picking costs no erase cycle
          // The mode carries its own consequences: All Sides adopts the
          // horizontal margin everywhere and turns Dynamic Margins off.
          const margin_link::State linked =
              margin_link::applyMode({{look_.screenMargin, look_.screenMarginTop, look_.screenMarginBottom},
                                      look_.dynamicMargins,
                                      margin_link::toMode(look_.marginLinkMode)},
                                     margin_link::toMode(next));
          look_.screenMargin = linked.margins.horizontal;
          look_.screenMarginTop = linked.margins.top;
          look_.screenMarginBottom = linked.margins.bottom;
          look_.dynamicMargins = linked.dynamicMargins;
          look_.marginLinkMode = margin_link::toStored(linked.mode);
          commit();
        });
      } else {
        optionPopup_.show(rowNameId(row), labels, labelCount, *field, [this, field](int idx) {
          const auto next = static_cast<uint8_t>(idx);
          if (next == *field) return;  // re-picking costs no erase cycle
          *field = next;
          commit();
        });
      }
      requestUpdate();
      return;
    }
    case RowKind::Toggle:
      break;
  }

  if (row == Row::DebugBorders) {
    // A developer switch, not part of any book's look: always the global setting.
    SETTINGS.debugBorders = !SETTINGS.debugBorders;
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }
  uint8_t* field = lookField(row);
  if (!field) return;
  *field = !*field;
  commit();
  requestUpdate();
}

// The dialog hands the value back once, when it closes, so there is nothing to
// debounce any more: apply it and let the caller write it.
void TextSettingsActivity::setEditedValue(const Row row, const int value) {
  const uint8_t* field = lookField(row);
  const SettingInfo* info = settingOf(row);
  if (!field || !info) return;

  const int next = std::clamp(value, static_cast<int>(info->valueRange.min), static_cast<int>(info->valueRange.max));
  if (next == *field) return;
  applyNumber(row, next);
  requestUpdate();
}

bool TextSettingsActivity::handleCustomInput() {
  if (optionPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) return true;  // picker owns input
  return false;
}

void TextSettingsActivity::onBackButton() { finish(); }

bool TextSettingsActivity::drawOverlay() { return optionPopup_.processRender(renderer, mappedInput); }

ListChrome TextSettingsActivity::chrome() const {
  ListChrome chrome;
  chrome.title = tr(STR_TEXT_SETTINGS);

  const char* confirmLabel = tr(STR_SELECT);
  const auto rows = visibleRows();
  if (selected() < static_cast<int>(rows.size())) {
    const RowKind kind = kindOf(rows[selected()]);
    if (kind == RowKind::Toggle) {
      confirmLabel = tr(STR_TOGGLE);
    } else if (kind == RowKind::Number) {
      confirmLabel = tr(STR_ADJUST);
    }
  }

  // Back closes the screen: a number is edited on its own screen now, so nothing
  // here is ever mid-edit.
  chrome.confirmHint = confirmLabel;
  chrome.thirdHint = tr(STR_DIR_UP);
  chrome.fourthHint = tr(STR_DIR_DOWN);
  return chrome;
}

// Font switching runs on the main task from loop(), which deliberately holds no
// RenderLock. ensureLoaded() deletes the resident SdCardFont before loading the next one,
// and the render task walks that same object inside the preview's prewarmCache() — so
// without this lock a font switch can free the mini glyph arrays out from under
// prewarmStyle() (crash: null s.miniGlyphs mid-read/sort).
void TextSettingsActivity::applyFamily(int listIndex) {
  // Saved on the way out of every path below, for the same reason applySize() does.
  struct SaveOnReturn {
    TextSettingsActivity& self;
    ~SaveOnReturn() { self.commit(); }
  } saveOnReturn{*this};

  RenderLock lock;
  const auto& font = fonts_[listIndex];
  if (font.isBuiltin) {
    look_.fontFamily = font.settingIndex;
    look_.sdFontFamilyName[0] = '\0';
    loadLookFont();  // unloads the previously resident SD font
    currentFamilyIndex_ = listIndex;
  } else if (registry_) {
    const int sdIdx = font.settingIndex - CrossPointSettings::BUILTIN_FONT_COUNT;
    const auto& families = registry_->getFamilies();
    if (sdIdx < static_cast<int>(families.size())) {
      strncpy(look_.sdFontFamilyName, families[sdIdx].name.c_str(), sizeof(look_.sdFontFamilyName) - 1);
      look_.sdFontFamilyName[sizeof(look_.sdFontFamilyName) - 1] = '\0';
      loadLookFont();
      currentFamilyIndex_ = listIndex;
    }
  }

  if (currentFamilyIndex_ != listIndex) return;  // switch failed — keep the old size list

  // The new family ships its own set of point sizes, and ensureLoaded() may have snapped
  // the selection into it, so the size list has to be rebuilt.
  rebuildSizeList();
}

// Same RenderLock rationale as applyFamily(): a size change reloads the SD font file,
// which frees and replaces the SdCardFont the render task may be reading.
void TextSettingsActivity::applySize(int listIndex) {
  {
    RenderLock lock;

    currentSizeIndex_ = listIndex;
    look_.fontPointSize = sizes_[listIndex].pointSize;
    loadLookFont();
  }
  // Persist outside the render lock, like the toggle rows do: the size is otherwise only
  // written when the screen is left, so powering off from inside Text Settings lost the
  // change (upstream #2806).
  commit();
}
