#include "TextSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "TextSettingsPreview.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Tab labels for Font | Size | Layout | Style (shared by render and loop touch hit-testing).
constexpr StrId TAB_NAME_IDS[] = {StrId::STR_FONT, StrId::STR_SIZE, StrId::STR_LAYOUT, StrId::STR_STYLE};

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

int findCurrentFontSizeIndex(uint8_t fontSize, size_t listSize) {
  return fontSize < listSize ? fontSize : 1;  // default MEDIUM
}

constexpr StrId ALIGNMENT_IDS[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                                   StrId::STR_BOOK_S_STYLE};
constexpr int MARGIN_MIN = CrossPointSettings::SCREEN_MARGIN_MIN;
constexpr int MARGIN_MAX = CrossPointSettings::SCREEN_MARGIN_MAX;
// First-line indent custom-% range (Book vs Custom% mode lives in CrossPointSettings).
constexpr int INDENT_MIN = 0;
constexpr int INDENT_MAX = CrossPointSettings::MAX_FIRST_LINE_INDENT_PERCENT;
constexpr int PARA_SPACING_MIN = 0;
constexpr int PARA_SPACING_MAX = CrossPointSettings::MAX_PARAGRAPH_SPACING;
// Dynamic margins picker (Off / mode 1 / mode 2) and first-line indent mode picker.
constexpr StrId DYNAMIC_MARGINS_IDS[] = {StrId::STR_DYNAMIC_MARGINS_OFF, StrId::STR_DYNAMIC_MARGINS_10,
                                         StrId::STR_DYNAMIC_MARGINS_20};
constexpr StrId INDENT_MODE_IDS[] = {StrId::STR_INDENT_BOOK, StrId::STR_INDENT_PERCENT};
}  // namespace

TextSettingsActivity::TextSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const SdCardFontRegistry* registry, Tab initialTab)
    : Activity("TextSettings", renderer, mappedInput), registry_(registry), tab_(initialTab) {}

void TextSettingsActivity::onEnter() {
  Activity::onEnter();

  metrics_ = UITheme::getInstance().getMetrics();
  afterHeader = metrics_.topPadding + metrics_.headerHeight + metrics_.verticalSpacing;
  bottomReserved = metrics_.buttonHintsHeight + metrics_.verticalSpacing;
  usableHeight = renderer.getScreenHeight() - afterHeader - bottomReserved;
  previewHeight = usableHeight * metrics_.previewHeightPercent / 100;

  fonts_.clear();
  fonts_.reserve(CrossPointSettings::BUILTIN_FONT_COUNT + (registry_ ? registry_->getFamilyCount() : 0));
  fonts_.push_back({I18N.get(StrId::STR_VOLLKORN), true, static_cast<uint8_t>(CrossPointSettings::VOLLKORN)});
  if (registry_) {
    const auto& families = registry_->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      fonts_.push_back({families[i].name, false, static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i)});
    }
  }

  sizes_.clear();
  sizes_.reserve(CrossPointSettings::FONT_SIZE_COUNT);
  sizes_.push_back({I18N.get(StrId::STR_SMALL), static_cast<uint8_t>(CrossPointSettings::SMALL)});
  sizes_.push_back({I18N.get(StrId::STR_MEDIUM), static_cast<uint8_t>(CrossPointSettings::MEDIUM)});
  sizes_.push_back({I18N.get(StrId::STR_LARGE), static_cast<uint8_t>(CrossPointSettings::LARGE)});
  sizes_.push_back({I18N.get(StrId::STR_X_LARGE), static_cast<uint8_t>(CrossPointSettings::EXTRA_LARGE)});

  currentFamilyIndex_ = findCurrentFontIndex(registry_, SETTINGS.sdFontFamilyName, SETTINGS.fontFamily);
  currentSizeIndex_ = findCurrentFontSizeIndex(SETTINGS.fontSize, sizes_.size());
  std::fill(std::begin(selectedIndex_), std::end(selectedIndex_), 1);       // default to the first list row
  selectedIndex_[static_cast<int>(Tab::Family)] = currentFamilyIndex_ + 1;  // Family/Size open on current selection
  selectedIndex_[static_cast<int>(Tab::Size)] = currentSizeIndex_ + 1;

  requestUpdate();
}

void TextSettingsActivity::onExit() { Activity::onExit(); }

TextSettingsActivity::PaneGeometry TextSettingsActivity::paneGeometry() const {
  const int previewTop = afterHeader;
  const int tabTop = previewTop + previewHeight;
  const int captionH = renderer.getTextHeight(UI_10_FONT_ID) + metrics_.verticalSpacing;
  const int listTop = tabTop + metrics_.tabBarHeight + metrics_.verticalSpacing;
  const int listHeight = usableHeight - previewHeight - metrics_.tabBarHeight - metrics_.verticalSpacing - captionH;
  return {previewTop, tabTop, listTop, listHeight};
}

void TextSettingsActivity::loop() {
  if (valueBar_.handleInput(mappedInput, [this] { requestUpdate(); })) return;     // bar owns input while open
  if (optionPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) return;  // picker owns input while open

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedIndex() == 0) {
      switchTab();
      return;
    }

    activateRow(selectedIndex() - 1);
    return;
  }

  const int ringSize = currentListSize() + 1;  // +1 for the tab bar at position 0

  buttonNavigator_.onNextRelease([this, ringSize] {
    selectedIndex() = ButtonNavigator::nextIndex(selectedIndex(), ringSize);
    requestUpdate();
  });

  buttonNavigator_.onPreviousRelease([this, ringSize] {
    selectedIndex() = ButtonNavigator::previousIndex(selectedIndex(), ringSize);
    requestUpdate();
  });

  buttonNavigator_.onNextContinuous([this] { switchTab(); });
  buttonNavigator_.onPreviousContinuous([this] { switchTab(-1); });
}

void TextSettingsActivity::render(RenderLock&&) {
  if (valueBar_.processRender(renderer, mappedInput)) return;     // bar draws over the preview
  if (optionPopup_.processRender(renderer, mappedInput)) return;  // picker draws over everything

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics_.topPadding, pageWidth, metrics_.headerHeight}, tr(STR_TEXT_SETTINGS));

  const auto geo = paneGeometry();
  const char* familyName = (currentFamilyIndex_ >= 0 && currentFamilyIndex_ < static_cast<int>(fonts_.size()))
                               ? fonts_[currentFamilyIndex_].name.c_str()
                               : "";
  const char* sizeName = (currentSizeIndex_ >= 0 && currentSizeIndex_ < static_cast<int>(sizes_.size()))
                             ? sizes_[currentSizeIndex_].name.c_str()
                             : "";
  textsettings::renderPreview(renderer, previewLayout_, metrics_.previewPadding, metrics_.verticalSpacing,
                              geo.previewTop, previewHeight, familyName, sizeName);

  const bool onTabBar = selectedIndex() == 0;
  std::vector<TabInfo> tabs;
  tabs.reserve(static_cast<int>(Tab::Count));
  for (int t = 0; t < static_cast<int>(Tab::Count); t++) {
    tabs.push_back({I18N.get(TAB_NAME_IDS[t]), tab_ == static_cast<Tab>(t)});
  }
  GUI.drawTabBar(renderer, Rect{0, geo.tabTop, pageWidth, metrics_.tabBarHeight}, tabs, onTabBar);

  const Rect listRect{0, geo.listTop, pageWidth, geo.listHeight};
  const int selectedItem = selectedIndex() - 1;
  const char* confirmLabel = tr(STR_SELECT);

  switch (tab_) {
    case Tab::Family:
      GUI.drawList(
          renderer, listRect, static_cast<int>(fonts_.size()), selectedItem,
          [this](int index) { return fonts_[index].name; }, nullptr, nullptr,
          [this](int index) -> std::string { return index == currentFamilyIndex_ ? tr(STR_SELECTED) : ""; }, true);
      if (onTabBar) confirmLabel = tr(STR_SIZE);
      break;

    case Tab::Size:
      GUI.drawList(
          renderer, listRect, static_cast<int>(sizes_.size()), selectedItem,
          [this](int index) { return sizes_[index].name; }, nullptr, nullptr,
          [this](int index) -> std::string { return index == currentSizeIndex_ ? tr(STR_SELECTED) : ""; }, true);
      if (onTabBar) confirmLabel = tr(STR_LAYOUT);
      break;

    case Tab::Layout: {
      const auto rows = visibleLayoutRows();
      const int layoutRows = static_cast<int>(rows.size());
      GUI.drawList(
          renderer, listRect, layoutRows, selectedItem, [this, &rows](int index) { return layoutRowName(rows[index]); },
          nullptr, nullptr, [this, &rows](int index) { return layoutValueText(rows[index]); }, true);
      if (onTabBar) {
        confirmLabel = tr(STR_STYLE);
      } else if (selectedItem >= 0 && selectedItem < layoutRows) {
        // Toggle rows say "Toggle"; bars and pickers say "Select".
        confirmLabel = isLayoutToggleRow(rows[selectedItem]) ? tr(STR_TOGGLE) : tr(STR_SELECT);
      }
      break;
    }

    case Tab::Style: {
      constexpr int STYLE_ROWS = static_cast<int>(StyleRow::Count);
      static constexpr StrId ROW_NAME_IDS[STYLE_ROWS] = {StrId::STR_FOCUS_READING, StrId::STR_GUIDE_DOTS,
                                                         StrId::STR_HYPHENATION, StrId::STR_EMBEDDED_STYLE,
                                                         StrId::STR_TEXT_AA};
      GUI.drawList(
          renderer, listRect, STYLE_ROWS, selectedItem,
          [](int index) { return std::string(I18N.get(ROW_NAME_IDS[index])); }, nullptr, nullptr,
          [this](int index) { return styleValueText(index); }, true);
      confirmLabel = onTabBar ? tr(STR_FONT) : tr(STR_TOGGLE);
      break;
    }

    default:
      break;
  }

  if (focusedRowHasNoPreview()) {
    const int capY = geo.listTop + geo.listHeight + metrics_.verticalSpacing;
    renderer.drawText(UI_10_FONT_ID, metrics_.previewPadding, capY, tr(STR_NOT_IN_PREVIEW));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

// Font switching runs on the main task from loop(), which deliberately holds no
// RenderLock. ensureLoaded() deletes the resident SdCardFont before loading the
// next one, and the render task walks that same object inside the preview's
// prewarmCache() — so without this lock a font switch can free the mini glyph
// arrays out from under prewarmStyle() (crash: null s.miniGlyphs mid-read/sort).
void TextSettingsActivity::applyFamily(int listIndex) {
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
}

void TextSettingsActivity::activateRow(int row) {
  switch (tab_) {
    case Tab::Family:
      if (row != currentFamilyIndex_) {
        applyFamily(row);
        requestUpdate();
      }
      break;
    case Tab::Size:
      if (row != currentSizeIndex_) {
        applySize(row);
        requestUpdate();
      }
      break;
    case Tab::Layout: {
      const auto rows = visibleLayoutRows();
      if (row >= 0 && row < static_cast<int>(rows.size())) confirmLayoutRow(rows[row]);
      break;
    }
    case Tab::Style:
      confirmStyleRow(row);
      break;
    default:
      break;
  }
}

// Same RenderLock rationale as applyFamily(): a size change reloads the SD font
// file, which frees and replaces the SdCardFont the render task may be reading.
void TextSettingsActivity::applySize(int listIndex) {
  RenderLock lock;

  currentSizeIndex_ = listIndex;
  SETTINGS.fontSize = sizes_[listIndex].settingIndex;
  sdFontSystem.ensureLoaded(renderer);
}

std::vector<TextSettingsActivity::LayoutRow> TextSettingsActivity::visibleLayoutRows() const {
  std::vector<LayoutRow> rows;
  rows.reserve(static_cast<int>(LayoutRow::Count));
  rows.push_back(LayoutRow::LineSpacing);
  rows.push_back(LayoutRow::ParaSpacing);
  rows.push_back(LayoutRow::ParaSpacingPct);
  rows.push_back(LayoutRow::Alignment);
  rows.push_back(LayoutRow::UniformMargins);
  rows.push_back(LayoutRow::ScreenMargin);
  // Independent top/bottom margins only make sense when uniform margins are off.
  if (!SETTINGS.uniformMargins) {
    rows.push_back(LayoutRow::ScreenMarginTop);
    rows.push_back(LayoutRow::ScreenMarginBottom);
  }
  rows.push_back(LayoutRow::DynamicMargins);
  rows.push_back(LayoutRow::IndentMode);
  // The custom-% bar only applies in Custom% mode; in Book mode the indent comes
  // from the EPUB's own CSS, so there is nothing to tune.
  if (SETTINGS.firstLineIndentMode == CrossPointSettings::FIRST_LINE_INDENT_PERCENT) {
    rows.push_back(LayoutRow::IndentPercent);
  }
  rows.push_back(LayoutRow::DebugBorders);
  return rows;
}

std::string TextSettingsActivity::layoutRowName(LayoutRow row) const {
  switch (row) {
    case LayoutRow::LineSpacing:
      return I18N.get(StrId::STR_LINE_SPACING);
    case LayoutRow::ParaSpacing:
      return I18N.get(StrId::STR_EXTRA_SPACING);
    case LayoutRow::ParaSpacingPct:
      return I18N.get(StrId::STR_PARAGRAPH_SPACING);
    case LayoutRow::Alignment:
      return I18N.get(StrId::STR_ALIGNMENT);
    case LayoutRow::UniformMargins:
      return I18N.get(StrId::STR_UNIFORM_MARGINS);
    case LayoutRow::ScreenMargin:
      return I18N.get(StrId::STR_SCREEN_MARGIN);
    case LayoutRow::ScreenMarginTop:
      return I18N.get(StrId::STR_SCREEN_MARGIN_TOP);
    case LayoutRow::ScreenMarginBottom:
      return I18N.get(StrId::STR_SCREEN_MARGIN_BOTTOM);
    case LayoutRow::DynamicMargins:
      return I18N.get(StrId::STR_DYNAMIC_MARGINS);
    case LayoutRow::IndentMode:
      return I18N.get(StrId::STR_FIRST_LINE_INDENT);
    case LayoutRow::IndentPercent:
      return I18N.get(StrId::STR_FIRST_LINE_INDENT_PERCENT);
    case LayoutRow::DebugBorders:
      return I18N.get(StrId::STR_DEBUG_BORDERS);
    default:
      return "";
  }
}

bool TextSettingsActivity::isLayoutToggleRow(LayoutRow row) const {
  return row == LayoutRow::ParaSpacing || row == LayoutRow::UniformMargins || row == LayoutRow::DebugBorders;
}

void TextSettingsActivity::confirmLayoutRow(LayoutRow row) {
  switch (row) {
    case LayoutRow::LineSpacing:
      valueBar_.show(StrId::STR_LINE_SPACING, CrossPointSettings::MIN_LINE_SPACING_PERCENT,
                     CrossPointSettings::MAX_LINE_SPACING_PERCENT, 1, 5, SETTINGS.lineSpacingPercent,
                     StrId::STR_VALUE_BAR_STEP_HINT,
                     [](int v) { SETTINGS.lineSpacingPercent = static_cast<uint8_t>(v); });
      break;
    case LayoutRow::ParaSpacing:
      SETTINGS.extraParagraphSpacing = !SETTINGS.extraParagraphSpacing;
      break;
    case LayoutRow::ParaSpacingPct:
      valueBar_.show(StrId::STR_PARAGRAPH_SPACING, PARA_SPACING_MIN, PARA_SPACING_MAX, 1, 5, SETTINGS.paragraphSpacing,
                     StrId::STR_VALUE_BAR_STEP_HINT,
                     [](int v) { SETTINGS.paragraphSpacing = static_cast<uint8_t>(v); });
      break;
    case LayoutRow::Alignment:
      optionPopup_.show(StrId::STR_ALIGNMENT, ALIGNMENT_IDS, static_cast<int>(std::size(ALIGNMENT_IDS)),
                        SETTINGS.paragraphAlignment,
                        [](int idx) { SETTINGS.paragraphAlignment = static_cast<uint8_t>(idx); });
      break;
    case LayoutRow::UniformMargins:
      SETTINGS.uniformMargins = !SETTINGS.uniformMargins;
      break;
    case LayoutRow::ScreenMargin:
      valueBar_.show(StrId::STR_SCREEN_MARGIN, MARGIN_MIN, MARGIN_MAX, 1, 5, SETTINGS.screenMargin,
                     StrId::STR_VALUE_BAR_STEP_HINT, [](int v) { SETTINGS.screenMargin = static_cast<uint8_t>(v); });
      break;
    case LayoutRow::ScreenMarginTop:
      valueBar_.show(StrId::STR_SCREEN_MARGIN_TOP, MARGIN_MIN, MARGIN_MAX, 1, 5, SETTINGS.screenMarginTop,
                     StrId::STR_VALUE_BAR_STEP_HINT, [](int v) { SETTINGS.screenMarginTop = static_cast<uint8_t>(v); });
      break;
    case LayoutRow::ScreenMarginBottom:
      valueBar_.show(StrId::STR_SCREEN_MARGIN_BOTTOM, MARGIN_MIN, MARGIN_MAX, 1, 5, SETTINGS.screenMarginBottom,
                     StrId::STR_VALUE_BAR_STEP_HINT,
                     [](int v) { SETTINGS.screenMarginBottom = static_cast<uint8_t>(v); });
      break;
    case LayoutRow::DynamicMargins:
      optionPopup_.show(StrId::STR_DYNAMIC_MARGINS, DYNAMIC_MARGINS_IDS,
                        static_cast<int>(std::size(DYNAMIC_MARGINS_IDS)), SETTINGS.dynamicMargins,
                        [](int idx) { SETTINGS.dynamicMargins = static_cast<uint8_t>(idx); });
      break;
    case LayoutRow::IndentMode:
      optionPopup_.show(StrId::STR_FIRST_LINE_INDENT, INDENT_MODE_IDS, static_cast<int>(std::size(INDENT_MODE_IDS)),
                        SETTINGS.firstLineIndentMode,
                        [](int idx) { SETTINGS.firstLineIndentMode = static_cast<uint8_t>(idx); });
      break;
    case LayoutRow::IndentPercent:
      valueBar_.show(StrId::STR_FIRST_LINE_INDENT_PERCENT, INDENT_MIN, INDENT_MAX, 1, 5,
                     SETTINGS.firstLineIndentPercent, StrId::STR_VALUE_BAR_STEP_HINT,
                     [](int v) { SETTINGS.firstLineIndentPercent = static_cast<uint8_t>(v); });
      break;
    case LayoutRow::DebugBorders:
      SETTINGS.debugBorders = !SETTINGS.debugBorders;
      break;
    default:
      break;
  }
  requestUpdate();
}

std::string TextSettingsActivity::layoutValueText(LayoutRow row) const {
  switch (row) {
    case LayoutRow::LineSpacing:
      return std::to_string(SETTINGS.lineSpacingPercent);
    case LayoutRow::ParaSpacing:
      return SETTINGS.extraParagraphSpacing ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case LayoutRow::ParaSpacingPct:
      return std::to_string(SETTINGS.paragraphSpacing);
    case LayoutRow::Alignment: {
      const uint8_t v = SETTINGS.paragraphAlignment;
      return v < std::size(ALIGNMENT_IDS) ? I18N.get(ALIGNMENT_IDS[v]) : I18N.get(StrId::STR_JUSTIFY);
    }
    case LayoutRow::UniformMargins:
      return SETTINGS.uniformMargins ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case LayoutRow::ScreenMargin:
      return std::to_string(SETTINGS.screenMargin);
    case LayoutRow::ScreenMarginTop:
      return std::to_string(SETTINGS.screenMarginTop);
    case LayoutRow::ScreenMarginBottom:
      return std::to_string(SETTINGS.screenMarginBottom);
    case LayoutRow::DynamicMargins: {
      const uint8_t v = SETTINGS.dynamicMargins;
      return v < std::size(DYNAMIC_MARGINS_IDS) ? I18N.get(DYNAMIC_MARGINS_IDS[v])
                                                : I18N.get(StrId::STR_DYNAMIC_MARGINS_OFF);
    }
    case LayoutRow::IndentMode: {
      const uint8_t v = SETTINGS.firstLineIndentMode;
      return v < std::size(INDENT_MODE_IDS) ? I18N.get(INDENT_MODE_IDS[v]) : I18N.get(StrId::STR_INDENT_BOOK);
    }
    case LayoutRow::IndentPercent:
      return std::to_string(SETTINGS.firstLineIndentPercent);
    case LayoutRow::DebugBorders:
      return SETTINGS.debugBorders ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    default:
      return "";
  }
}

void TextSettingsActivity::confirmStyleRow(int row) {
  switch (static_cast<StyleRow>(row)) {
    case StyleRow::FocusReading:
      SETTINGS.focusReadingEnabled = !SETTINGS.focusReadingEnabled;
      break;
    case StyleRow::GuideDots:
      SETTINGS.guideDotsEnabled = !SETTINGS.guideDotsEnabled;
      break;
    case StyleRow::Hyphenation:
      SETTINGS.hyphenationEnabled = !SETTINGS.hyphenationEnabled;
      break;
    case StyleRow::EmbeddedStyle:
      SETTINGS.embeddedStyle = !SETTINGS.embeddedStyle;
      break;
    case StyleRow::AntiAliasing:
      SETTINGS.textAntiAliasing = !SETTINGS.textAntiAliasing;
      break;

    default:
      return;
  }
  requestUpdate();
}

std::string TextSettingsActivity::styleValueText(int row) const {
  switch (static_cast<StyleRow>(row)) {
    case StyleRow::FocusReading:
      return SETTINGS.focusReadingEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case StyleRow::GuideDots:
      return SETTINGS.guideDotsEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case StyleRow::Hyphenation:
      return SETTINGS.hyphenationEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case StyleRow::EmbeddedStyle:
      return SETTINGS.embeddedStyle ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case StyleRow::AntiAliasing:
      return SETTINGS.textAntiAliasing ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);

    default:
      return "";
  }
}

// Only Focus Reading shows in the preview (bold prefixes); the other Style rows
// have no distinct preview.
bool TextSettingsActivity::focusedRowHasNoPreview() const {
  if (selectedIndex() == 0 || tab_ != Tab::Style) return false;
  const StyleRow row = static_cast<StyleRow>(selectedIndex() - 1);
  return row == StyleRow::GuideDots || row == StyleRow::Hyphenation || row == StyleRow::EmbeddedStyle ||
         row == StyleRow::AntiAliasing;
}

void TextSettingsActivity::switchTab(int direction) {
  const bool onTabBar = selectedIndex() == 0;
  constexpr int tabCount = static_cast<int>(Tab::Count);
  tab_ = static_cast<Tab>((static_cast<int>(tab_) + direction + tabCount) % tabCount);
  if (onTabBar) selectedIndex() = 0;
  requestUpdate();
}

int TextSettingsActivity::currentListSize() const {
  switch (tab_) {
    case Tab::Family:
      return static_cast<int>(fonts_.size());
    case Tab::Size:
      return static_cast<int>(sizes_.size());
    case Tab::Layout:
      return static_cast<int>(visibleLayoutRows().size());
    case Tab::Style:
      return static_cast<int>(StyleRow::Count);

    default:
      return 0;
  }
}

int& TextSettingsActivity::selectedIndex() { return selectedIndex_[static_cast<int>(tab_)]; }
int TextSettingsActivity::selectedIndex() const { return selectedIndex_[static_cast<int>(tab_)]; }
