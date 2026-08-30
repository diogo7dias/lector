#include "TextSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

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
#include "TextSettingsPreview.h"
#include "components/RowHitTest.h"
#include "components/UITheme.h"
#include "activities/settings/FontPickerActivity.h"
#include "fontIds.h"
#include "util/MarginLink.h"

namespace {
constexpr StrId ALIGNMENT_IDS[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                                   StrId::STR_BOOK_S_STYLE};
constexpr StrId MARGIN_LINK_IDS[] = {StrId::STR_MARGIN_LINK_OFF, StrId::STR_MARGIN_LINK_TOP_BOTTOM,
                                     StrId::STR_MARGIN_LINK_ALL};
constexpr StrId DYNAMIC_MARGINS_IDS[] = {StrId::STR_DYNAMIC_MARGINS_OFF, StrId::STR_DYNAMIC_MARGINS_10,
                                         StrId::STR_DYNAMIC_MARGINS_20};
constexpr StrId INDENT_MODE_IDS[] = {StrId::STR_INDENT_BOOK, StrId::STR_INDENT_PERCENT};

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

// Holding Up on an armed number steps it far faster than e-ink can follow. Redraw once the
// value has been still this long; the value itself moves at full speed in the row.
constexpr uint32_t EDIT_REDRAW_DEBOUNCE_MS = 200;
// Holding Up or Down on a numeric row steps the value once per repeat. Writing the
// settings file on each of those steps would rewrite the whole file dozens of times for
// one margin sweep, and SPIFFS sectors have a finite erase cycle limit (CLAUDE.md,
// Resource Protocol 8). The value is written once it stops moving instead.
constexpr uint32_t EDIT_SAVE_DEBOUNCE_MS = 1200;

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
                                           const SdCardFontRegistry* registry)
    : UiGridActivity("TextSettings", renderer, mappedInput), registry_(registry) {}

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
  currentFamilyIndex_ = findCurrentFontIndex(registry_, SETTINGS.sdFontFamilyName, SETTINGS.fontFamily);
  setSelected(0);

  requestUpdate();
}

void TextSettingsActivity::onExit() {
  commitSettings();
  Activity::onExit();
}

// The selectable sizes belong to the active family, so this runs on entry and again after
// every family change. A family change goes through ensureLoaded(), which snaps
// SETTINGS.fontPointSize into the new family's set — but entry does not, so the highlight
// is resolved by snapping rather than by exact match.
void TextSettingsActivity::rebuildSizeList() {
  const std::vector<uint8_t> points = readerFontPointSizes(registry_, SETTINGS.sdFontFamilyName);

  // The stored size can still sit outside this family's set — e.g. the family was deleted
  // while selected, or the card was swapped. Highlight the size the reader actually
  // renders, which getReaderFontId() resolves the same way.
  const uint8_t selectedPt = snapToNearestPointSize(points, SETTINGS.fontPointSize);

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
  textsettings::renderPreview(renderer, previewLayout_, rect.y, rect.height - metrics_.verticalSpacing);
}

TextSettingsActivity::RowKind TextSettingsActivity::kindOf(const Row row) {
  switch (row) {
    case Row::Font:
      return RowKind::FontList;
    case Row::Size:
    case Row::Alignment:
    case Row::IndentMode:
    case Row::MarginLink:
    case Row::DynamicMargins:
      return RowKind::Picker;
    case Row::LineSpacing:
    case Row::ParagraphSpacing:  // retired in 0.8.2; the row is still wired, just not listed
    case Row::IndentPercent:
    case Row::HorizontalMargin:
    case Row::VerticalMargin:
    case Row::TopMargin:
    case Row::BottomMargin:
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
  rows.reserve(24);

  rows.push_back(Row::Font);
  rows.push_back(Row::Size);

  // The two that change how the ink itself sits on the page.
  rows.push_back(Row::PaperbackLook);
  rows.push_back(Row::AntiAliasing);

  rows.push_back(Row::LineSpacing);
  rows.push_back(Row::ExtraSpacing);

  rows.push_back(Row::Alignment);
  rows.push_back(Row::IndentMode);
  // The custom-% value only applies in Custom% mode; in Book mode the indent comes from
  // the EPUB's own CSS, so there is nothing to tune.
  if (SETTINGS.firstLineIndentMode == CrossPointSettings::FIRST_LINE_INDENT_PERCENT) {
    rows.push_back(Row::IndentPercent);
  }

  // All Sides: the horizontal cell is every side, so it is the only margin cell, and
  // Dynamic Margins is not offered at all — it would compute a horizontal margin of its
  // own and leave left/right disagreeing with top/bottom.
  rows.push_back(Row::HorizontalMargin);
  rows.push_back(Row::MarginLink);
  switch (margin_link::toMode(SETTINGS.marginLinkMode)) {
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

  rows.push_back(Row::Hyphenation);
  rows.push_back(Row::FocusReading);

  rows.push_back(Row::GuideDots);
  // Hidden Dots only says anything about a page that is already drawing guide dots.
  if (SETTINGS.guideDotsEnabled) rows.push_back(Row::HiddenDots);

  rows.push_back(Row::EmbeddedTextStyle);
  rows.push_back(Row::EmbeddedLayoutStyle);

  rows.push_back(Row::DebugBorders);
  return rows;
}

StrId TextSettingsActivity::rowNameId(const Row row) const {
  switch (row) {
    case Row::Font:
      return StrId::STR_FONT;
    case Row::Size:
      return StrId::STR_SIZE;
    case Row::PaperbackLook:
      return StrId::STR_PAPERBACK_LOOK;
    case Row::LineSpacing:
      return StrId::STR_LINE_SPACING;
    case Row::ExtraSpacing:
      return StrId::STR_EXTRA_SPACING;
    case Row::ParagraphSpacing:
      return StrId::STR_PARAGRAPH_SPACING;
    case Row::Alignment:
      return StrId::STR_ALIGNMENT;
    case Row::IndentMode:
      return StrId::STR_FIRST_LINE_INDENT;
    case Row::IndentPercent:
      return StrId::STR_FIRST_LINE_INDENT_PERCENT;
    case Row::HorizontalMargin:
      // In All Sides this row is the only margin there is, so naming it "Horizontal"
      // would be describing a side rather than what it does.
      return margin_link::toMode(SETTINGS.marginLinkMode) == margin_link::Mode::AllSides ? StrId::STR_MARGIN
                                                                                         : StrId::STR_HORIZONTAL_MARGIN;
    case Row::MarginLink:
      return StrId::STR_LINK_MARGINS;
    case Row::VerticalMargin:
      return StrId::STR_VERTICAL_MARGIN;
    case Row::TopMargin:
      return StrId::STR_SCREEN_MARGIN_TOP;
    case Row::BottomMargin:
      return StrId::STR_SCREEN_MARGIN_BOTTOM;
    case Row::DynamicMargins:
      return StrId::STR_DYNAMIC_MARGINS;
    case Row::FocusReading:
      return StrId::STR_FOCUS_READING;
    case Row::GuideDots:
      return StrId::STR_GUIDE_DOTS;
    case Row::HiddenDots:
      return StrId::STR_HIDDEN_DOTS;
    case Row::Hyphenation:
      return StrId::STR_HYPHENATION;
    case Row::EmbeddedTextStyle:
      return StrId::STR_EMBEDDED_TEXT_STYLE;
    case Row::EmbeddedLayoutStyle:
      return StrId::STR_EMBEDDED_LAYOUT_STYLE;
    case Row::AntiAliasing:
      return StrId::STR_TEXT_AA;
    default:
      return StrId::STR_DEBUG_BORDERS;
  }
}

// The vertical margins are two stored fields even while they are linked, so the linked row
// edits the top one and applyNumber() mirrors it into the bottom.
uint8_t* TextSettingsActivity::numberField(const Row row) const {
  switch (row) {
    case Row::LineSpacing:
      return &SETTINGS.lineSpacingPercent;
    case Row::ParagraphSpacing:
      return &SETTINGS.paragraphSpacing;
    case Row::IndentPercent:
      return &SETTINGS.firstLineIndentPercent;
    case Row::HorizontalMargin:
      return &SETTINGS.screenMargin;
    case Row::VerticalMargin:
    case Row::TopMargin:
      return &SETTINGS.screenMarginTop;
    case Row::BottomMargin:
      return &SETTINGS.screenMarginBottom;
    default:
      return nullptr;
  }
}

void TextSettingsActivity::numberRange(const Row row, int& minValue, int& maxValue) const {
  switch (row) {
    case Row::LineSpacing:
      minValue = CrossPointSettings::MIN_LINE_SPACING_PERCENT;
      maxValue = CrossPointSettings::MAX_LINE_SPACING_PERCENT;
      break;
    case Row::ParagraphSpacing:
      minValue = 0;
      maxValue = CrossPointSettings::MAX_PARAGRAPH_SPACING;
      break;
    case Row::IndentPercent:
      minValue = 0;
      maxValue = CrossPointSettings::MAX_FIRST_LINE_INDENT_PERCENT;
      break;
    default:
      minValue = CrossPointSettings::SCREEN_MARGIN_MIN;
      maxValue = CrossPointSettings::SCREEN_MARGIN_MAX;
      break;
  }
}

void TextSettingsActivity::applyNumber(const Row row, const int value) {
  uint8_t* field = numberField(row);
  if (!field) return;
  const margin_link::Margins current{SETTINGS.screenMargin, SETTINGS.screenMarginTop, SETTINGS.screenMarginBottom};
  const margin_link::Mode mode = margin_link::toMode(SETTINGS.marginLinkMode);
  const auto write = [](const margin_link::Margins next) {
    SETTINGS.screenMargin = next.horizontal;
    SETTINGS.screenMarginTop = next.top;
    SETTINGS.screenMarginBottom = next.bottom;
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
// when the value stops moving, when the row is left, and on the way out of the screen,
// so powering off or sleeping from inside Text Settings cannot lose the change
// (the same failure applySize() guards against, upstream #2806).
void TextSettingsActivity::commitSettings() {
  pendingSaveAt_ = 0;
  if (!settingsDirty_) return;
  settingsDirty_ = false;
  SETTINGS.saveToFile();
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
    case Row::ExtraSpacing:
      return onOff(SETTINGS.extraParagraphSpacing);
    case Row::Alignment: {
      const uint8_t v = SETTINGS.paragraphAlignment;
      const std::string label =
          v < std::size(ALIGNMENT_IDS) ? I18N.get(ALIGNMENT_IDS[v]) : I18N.get(StrId::STR_JUSTIFY);
      // "Book's Style" reads the alignment out of the book's own CSS, which is exactly what
      // Embedded Layout Style switches off. Saying so on the row beats a setting that looks
      // chosen and does nothing.
      return (v == ALIGNMENT_BOOK_INDEX && !SETTINGS.embeddedLayoutStyle) ? needsLayoutLabel(label) : label;
    }
    case Row::IndentMode: {
      const uint8_t v = SETTINGS.firstLineIndentMode;
      const std::string label =
          v < std::size(INDENT_MODE_IDS) ? I18N.get(INDENT_MODE_IDS[v]) : I18N.get(StrId::STR_INDENT_BOOK);
      return (v == INDENT_MODE_BOOK_INDEX && !SETTINGS.embeddedLayoutStyle) ? needsLayoutLabel(label) : label;
    }
    case Row::MarginLink: {
      const uint8_t v = SETTINGS.marginLinkMode;
      return v < std::size(MARGIN_LINK_IDS) ? I18N.get(MARGIN_LINK_IDS[v]) : I18N.get(StrId::STR_MARGIN_LINK_OFF);
    }
    case Row::DynamicMargins: {
      const uint8_t v = SETTINGS.dynamicMargins;
      return v < std::size(DYNAMIC_MARGINS_IDS) ? I18N.get(DYNAMIC_MARGINS_IDS[v])
                                                : I18N.get(StrId::STR_DYNAMIC_MARGINS_OFF);
    }
    case Row::FocusReading:
      return onOff(SETTINGS.focusReadingEnabled);
    case Row::GuideDots:
      return onOff(SETTINGS.guideDotsEnabled);
    case Row::HiddenDots:
      return onOff(SETTINGS.guideDotsHidden);
    case Row::Hyphenation:
      return onOff(SETTINGS.hyphenationEnabled);
    case Row::EmbeddedTextStyle:
      return onOff(SETTINGS.embeddedTextStyle);
    case Row::EmbeddedLayoutStyle:
      return onOff(SETTINGS.embeddedLayoutStyle);
    case Row::AntiAliasing:
      return onOff(SETTINGS.textAntiAliasing);
    case Row::PaperbackLook:
      return onOff(SETTINGS.paperbackLookBody);
    case Row::DebugBorders:
      return onOff(SETTINGS.debugBorders);
    default:
      break;
  }
  if (const uint8_t* field = numberField(row)) return std::to_string(*field);
  return "";
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
      int minValue = 0;
      int maxValue = 0;
      numberRange(row, minValue, maxValue);
      const uint8_t* field = numberField(row);
      editing_ = true;
      const auto& metrics = UITheme::getInstance().getMetrics();
      armValueBand(I18N.get(rowNameId(row)), minValue, maxValue, /*smallStep=*/1, /*largeStep=*/5,
                   field ? *field : minValue, [this](const int chosen) { setEditedValue(chosen); },
                   [this] { leaveEdit(); });
      return;
    }
    case RowKind::Picker:
      switch (row) {
        case Row::Size:
          openSizePicker();
          break;
        case Row::Alignment:
          optionPopup_.show(StrId::STR_ALIGNMENT, ALIGNMENT_IDS, static_cast<int>(std::size(ALIGNMENT_IDS)),
                            SETTINGS.paragraphAlignment, [](int idx) {
                              const auto next = static_cast<uint8_t>(idx);
                              if (next == SETTINGS.paragraphAlignment) return;  // re-picking costs no erase cycle
                              SETTINGS.paragraphAlignment = next;
                              SETTINGS.saveToFile();
                            });
          break;
        case Row::IndentMode:
          optionPopup_.show(StrId::STR_FIRST_LINE_INDENT, INDENT_MODE_IDS, static_cast<int>(std::size(INDENT_MODE_IDS)),
                            SETTINGS.firstLineIndentMode, [](int idx) {
                              const auto next = static_cast<uint8_t>(idx);
                              if (next == SETTINGS.firstLineIndentMode) return;  // re-picking costs no erase cycle
                              SETTINGS.firstLineIndentMode = next;
                              SETTINGS.saveToFile();
                            });
          break;
        case Row::MarginLink:
          optionPopup_.show(StrId::STR_LINK_MARGINS, MARGIN_LINK_IDS, static_cast<int>(std::size(MARGIN_LINK_IDS)),
                            SETTINGS.marginLinkMode, [](int idx) {
                              const auto next = static_cast<uint8_t>(idx);
                              if (next == SETTINGS.marginLinkMode) return;  // re-picking costs no erase cycle
                              // The mode carries its own consequences: All Sides adopts the
                              // horizontal margin everywhere and turns Dynamic Margins off.
                              SETTINGS.setMarginLinkMode(margin_link::toMode(next));
                              SETTINGS.saveToFile();
                            });
          break;
        default:
          optionPopup_.show(StrId::STR_DYNAMIC_MARGINS, DYNAMIC_MARGINS_IDS,
                            static_cast<int>(std::size(DYNAMIC_MARGINS_IDS)), SETTINGS.dynamicMargins, [](int idx) {
                              const auto next = static_cast<uint8_t>(idx);
                              if (next == SETTINGS.dynamicMargins) return;  // re-picking costs no erase cycle
                              SETTINGS.dynamicMargins = next;
                              SETTINGS.saveToFile();
                            });
          break;
      }
      requestUpdate();
      return;
    case RowKind::Toggle:
      break;
  }

  switch (row) {
    case Row::ExtraSpacing:
      SETTINGS.extraParagraphSpacing = !SETTINGS.extraParagraphSpacing;
      break;
    case Row::FocusReading:
      SETTINGS.focusReadingEnabled = !SETTINGS.focusReadingEnabled;
      break;
    case Row::GuideDots:
      SETTINGS.guideDotsEnabled = !SETTINGS.guideDotsEnabled;
      break;
    case Row::HiddenDots:
      SETTINGS.guideDotsHidden = !SETTINGS.guideDotsHidden;
      break;
    case Row::Hyphenation:
      SETTINGS.hyphenationEnabled = !SETTINGS.hyphenationEnabled;
      break;
    case Row::EmbeddedTextStyle:
      SETTINGS.embeddedTextStyle = !SETTINGS.embeddedTextStyle;
      break;
    case Row::EmbeddedLayoutStyle:
      SETTINGS.embeddedLayoutStyle = !SETTINGS.embeddedLayoutStyle;
      break;
    case Row::AntiAliasing:
      SETTINGS.textAntiAliasing = !SETTINGS.textAntiAliasing;
      break;
    case Row::PaperbackLook:
      SETTINGS.paperbackLookBody = !SETTINGS.paperbackLookBody;
      break;
    case Row::DebugBorders:
      SETTINGS.debugBorders = !SETTINGS.debugBorders;
      break;
    default:
      return;
  }
  SETTINGS.saveToFile();
  requestUpdate();
}

void TextSettingsActivity::setEditedValue(const int value) {
  const auto rows = visibleRows();
  if (selected() >= static_cast<int>(rows.size())) return;
  const Row row = rows[selected()];
  const uint8_t* field = numberField(row);
  if (!field) return;

  int minValue = 0, maxValue = 0;
  numberRange(row, minValue, maxValue);
  const int next = std::clamp(value, minValue, maxValue);
  if (next == *field) return;
  applyNumber(row, next);
  // The band and the cell follow the finger; the preview and the write wait for the value
  // to settle, so a drag across the whole range costs one preview pass and one write.
  const uint32_t now = millis();
  pendingRedrawAt_ = now + EDIT_REDRAW_DEBOUNCE_MS;
  pendingSaveAt_ = now + EDIT_SAVE_DEBOUNCE_MS;
  requestUpdate();
}

void TextSettingsActivity::leaveEdit() {
  editing_ = false;
  pendingRedrawAt_ = 0;
  commitSettings();
  requestUpdate();
}

bool TextSettingsActivity::handleCustomInput() {
  if (optionPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) return true;  // picker owns input

  // A debounced preview redraw that came due while the value sat still.
  if (pendingRedrawAt_ != 0 && millis() >= pendingRedrawAt_) {
    pendingRedrawAt_ = 0;
    requestUpdate();
  }
  // The value has stopped moving: write it once.
  if (pendingSaveAt_ != 0 && millis() >= pendingSaveAt_) commitSettings();
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

  // Back closes the band rather than the screen while a value is armed, and the
  // hint has to say so or it reads as a way out of Text Settings.
  if (editing_) {
    chrome.backHint = tr(STR_DONE_EDIT);
    chrome.confirmHint = tr(STR_DONE_EDIT);
    chrome.thirdHint = "-";
    chrome.fourthHint = "+";
    return chrome;
  }
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
    ~SaveOnReturn() { SETTINGS.saveToFile(); }
  } saveOnReturn;

  RenderLock lock;
  const auto& font = fonts_[listIndex];
  if (font.isBuiltin) {
    SETTINGS.fontFamily = font.settingIndex;
    SETTINGS.sdFontFamilyName[0] = '\0';
    sdFontSystem.ensureLoaded(renderer);  // unloads the previously resident SD font
    currentFamilyIndex_ = listIndex;
  } else if (registry_) {
    const int sdIdx = font.settingIndex - CrossPointSettings::BUILTIN_FONT_COUNT;
    const auto& families = registry_->getFamilies();
    if (sdIdx < static_cast<int>(families.size())) {
      strncpy(SETTINGS.sdFontFamilyName, families[sdIdx].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
      SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
      sdFontSystem.ensureLoaded(renderer);
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
    SETTINGS.fontPointSize = sizes_[listIndex].pointSize;
    sdFontSystem.ensureLoaded(renderer);
  }
  // Persist outside the render lock, like the toggle rows do: the size is otherwise only
  // written when the screen is left, so powering off from inside Text Settings lost the
  // change (upstream #2806).
  SETTINGS.saveToFile();
}
