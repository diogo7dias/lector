#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "components/StatusBar.h"  // StatusBarData for the v2 status bar
#include "fontIds.h"               // UI_10_FONT_ID default for drawList

class GfxRenderer;
struct RecentBook;

// Which item indices a variable-height list actually rendered this frame, so the caller
// can keep the selected row on screen. Used by drawRecentBookList and drawWrappedList.
struct ListVisibility {
  int firstVisible;  // index of the first fully-rendered book
  int lastVisible;   // index of the last fully-rendered book (inclusive)
  int totalCount;
};

struct Rect {
  int x;
  int y;
  int width;
  int height;

  explicit Rect(int x = 0, int y = 0, int width = 0, int height = 0) : x(x), y(y), width(width), height(height) {}
};

struct TabInfo {
  const char* label;
  bool selected;
};

struct ThemeMetrics {
  int batteryWidth;
  int batteryHeight;

  int topPadding;
  int batteryBarHeight;
  int headerHeight;
  int verticalSpacing;

  int previewPadding;
  int previewHeightPercent;

  int contentSidePadding;
  int listRowHeight;
  int listWithSubtitleRowHeight;
  int menuRowHeight;
  int menuSpacing;

  int tabSpacing;
  int tabBarHeight;

  int scrollBarWidth;
  int scrollBarRightOffset;

  int homeTopPadding;
  int homeCoverHeight;
  int homeCoverTileHeight;
  int homeRecentBooksCount;
  bool homeContinueReadingInMenu;
  int homeMenuTopOffset;

  int buttonHintsHeight;
  int sideButtonHintsWidth;

  int progressBarHeight;
  int progressBarMarginTop;
  int statusBarHorizontalMargin;
  int statusBarVerticalMargin;
  int keyboardKeyHeight;
  int keyboardKeySpacing;
  bool keyboardCenteredText;
  int keyboardVerticalOffset;
  int keyboardTextFieldWidthPercent;
  int keyboardWidthPercent;

  int popupMarginX;
  int popupFrameThickness;
  int popupCornerRadius;
  int popupProgressBarHeight;
  bool popupProgressDrawOutline;
  bool popupProgressClampPercent;
  bool popupProgressFillInverted;
  bool popupProgressOutlineInverted;

  int optionPopupItemSpacing;
  int optionPopupInnerPadding;
  int optionPopupSelectionHPadding;
  int optionPopupSelectionVPadding;
  int optionPopupTitleGap;
  bool optionPopupUseSmallFont;
  bool optionPopupOptionFontBold;
  int optionPopupSelectionRadius;
  bool optionPopupSelectionLight;
  bool optionPopupDrawAllRows;
  int optionPopupDialogSideMargin;
  bool optionPopupTitleSeparator;

  int textFieldHorizontalPadding;
  int textFieldNormalThickness;
  int textFieldCursorThickness;
  int textFieldLineEndOffset;
};

enum UIIcon { None = 0, Folder, Text, Image, Book, File, Recent, Settings, Transfer, Library, Wifi, Hotspot, Bookmark };

// Default theme implementation (Classic Theme)
// Additional themes can inherit from this and override methods as needed

namespace BaseMetrics {
constexpr ThemeMetrics values = {.batteryWidth = 15,
                                 .batteryHeight = 12,
                                 // 5 px of chrome padding plus the X4's ~9 px physical top crop
                                 // (its BoardProfile::viewableInsets.top). Menu screens do not consult
                                 // getOrientedViewableTRBL, so without this the header sits right on
                                 // the panel edge. Every header y, content top and the per-page item
                                 // reserve are expressed against topPadding, so they all shift together.
                                 .topPadding = 14,
                                 .batteryBarHeight = 20,
                                 .headerHeight = 45,
                                 .verticalSpacing = 10,
                                 .previewPadding = 12,
                                 .previewHeightPercent = 30,
                                 .contentSidePadding = 20,
                                 .listRowHeight = 30,
                                 .listWithSubtitleRowHeight = 50,
                                 .menuRowHeight = 45,
                                 .menuSpacing = 8,
                                 .tabSpacing = 10,
                                 .tabBarHeight = 50,
                                 .scrollBarWidth = 4,
                                 .scrollBarRightOffset = 5,
                                 .homeTopPadding = 40,
                                 .homeCoverHeight = 400,
                                 .homeCoverTileHeight = 400,
                                 .homeRecentBooksCount = 1,
                                 .homeContinueReadingInMenu = false,
                                 .homeMenuTopOffset = 10,
                                 .buttonHintsHeight = 40,
                                 .sideButtonHintsWidth = 30,
                                 .progressBarHeight = 16,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 5,
                                 .statusBarVerticalMargin = 19,
                                 .keyboardKeyHeight = 48,
                                 .keyboardKeySpacing = 0,
                                 .keyboardCenteredText = false,
                                 .keyboardVerticalOffset = -13,
                                 .keyboardTextFieldWidthPercent = 85,
                                 .keyboardWidthPercent = 94,
                                 .popupMarginX = 15,
                                 .popupFrameThickness = 2,
                                 .popupCornerRadius = 0,
                                 .popupProgressBarHeight = 4,
                                 .popupProgressDrawOutline = false,
                                 .popupProgressClampPercent = false,
                                 // White on the strip's black backing.
                                 .popupProgressFillInverted = false,
                                 .popupProgressOutlineInverted = false,
                                 .optionPopupItemSpacing = 6,
                                 .optionPopupInnerPadding = 16,
                                 .optionPopupSelectionHPadding = 8,
                                 .optionPopupSelectionVPadding = 4,
                                 .optionPopupTitleGap = 10,
                                 .optionPopupUseSmallFont = true,
                                 .optionPopupOptionFontBold = true,
                                 .optionPopupSelectionRadius = 0,
                                 .optionPopupSelectionLight = false,
                                 .optionPopupDrawAllRows = false,
                                 .optionPopupDialogSideMargin = 20,
                                 .optionPopupTitleSeparator = true,
                                 .textFieldHorizontalPadding = 6,
                                 .textFieldNormalThickness = 1,
                                 .textFieldCursorThickness = 3,
                                 .textFieldLineEndOffset = 0};
}

class BaseTheme {
 public:
  virtual ~BaseTheme() = default;

  // Component drawing methods
  void drawProgressBar(const GfxRenderer& renderer, Rect rect, size_t current, size_t total) const;
  void drawBatteryLeft(const GfxRenderer& renderer, Rect rect, bool showPercentage = true,
                       int fontId = UI_10_FONT_ID) const;  // Left aligned (reader mode)
  void drawBatteryRight(const GfxRenderer& renderer, Rect rect,
                        bool showPercentage = true) const;  // Right aligned (UI headers)
  virtual void fillBatteryIcon(const GfxRenderer& renderer, Rect rect, uint16_t percentage) const;
  virtual void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                               const char* btn4) const;
  // Shared by every theme's drawButtonHints(): centres a hint label in its box,
  // wrapping to two lines rather than overflowing when it's too wide to fit.
  static void drawHintLabel(GfxRenderer& renderer, int fontId, const char* label, int x, int boxWidth, int boxTop,
                            int boxHeight, int singleLineYOffset);
  virtual void drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const;
  virtual int getListRowStep(bool hasSubtitle) const;
  virtual int getListPageItems(int contentHeight, bool hasSubtitle) const;
  virtual void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                        const std::function<std::string(int index)>& rowTitle,
                        const std::function<std::string(int index)>& rowSubtitle = nullptr,
                        const std::function<UIIcon(int index)>& rowIcon = nullptr,
                        const std::function<std::string(int index)>& rowValue = nullptr, bool highlightValue = false,
                        const std::function<bool(int index)>& rowDimmed = nullptr, int itemFontId = UI_10_FONT_ID,
                        // Rows for which this returns true are section headings: a label
                        // with a rule running out to the right edge, never selectable and
                        // never highlighted. They occupy a normal row slot, so paging and
                        // the selection maths are unchanged. The caller is responsible for
                        // skipping them when moving the selection.
                        const std::function<bool(int index)>& rowIsHeader = nullptr,
                        // Opt in to scrolling instead of paging. Left null, the list snaps its
                        // window to whole pages as it always has. Pass a caller-owned offset
                        // that survives between frames and the window instead slides by the
                        // least amount that keeps the selected row visible, so the rows around
                        // the cursor hold still as it moves. drawList writes the clamped offset
                        // back, so the caller never has to correct it (see ListScrollPolicy.h).
                        int* scrollOffset = nullptr) const;
  virtual void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                          const char* subtitle = nullptr) const;
  virtual void drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                             const char* rightLabel = nullptr) const;
  virtual void drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                          bool selected) const;
  virtual bool tabIndexFromPoint(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs, int x, int y,
                                 int& index) const;
  virtual void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                   const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                   bool& bufferRestored, std::function<bool()> storeCoverBuffer) const;
  // Home in-progress list: each book's full title + author initials wrapped across as
  // many lines as it needs, with an inline [NN%] black-background badge, the selected
  // row inverted, and "N more above/below" indicators when the list scrolls. Returns
  // the visible index range so the caller can keep the selected book on screen.
  virtual ListVisibility drawRecentBookList(GfxRenderer& renderer, Rect rect,
                                            const std::vector<RecentBook>& recentBooks, int selectorIndex,
                                            int scrollOffset) const;
  // Variable-height sibling of drawList: each row's title WRAPS over as many lines as it
  // needs instead of being ellipsised, so a long filename stays readable in full. rowValue
  // is optional and is drawn right-aligned on the row's first line, with its width reserved
  // there. Rows scroll rather than paginate, so the caller keeps a scrollOffset and feeds
  // back the returned visible range (see FileBrowserActivity).
  virtual ListVisibility drawWrappedList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                                         int scrollOffset, const std::function<std::string(int index)>& rowTitle,
                                         const std::function<std::string(int index)>& rowValue = nullptr,
                                         // Drawn as a filled chip before the title on the row's first line, in
                                         // the same style drawRecentBookList uses on the home screen: black on
                                         // an unselected row, white on the inverted one. Return an empty
                                         // string for a row that has no badge. The title wraps to the right of
                                         // the chip and its continuation lines stay under the first line, not
                                         // back at the left margin, so the text block keeps a straight edge.
                                         const std::function<std::string(int index)>& rowBadge = nullptr) const;
  virtual void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                              const std::function<std::string(int index)>& buttonLabel,
                              const std::function<UIIcon(int index)>& rowIcon) const;
  // The one message surface: a full-width black strip below the top padding, with a
  // white inset border and white centered text. Paints only — the caller picks the
  // refresh, because the busy banner wants the cheap FAST waveform and popups do not.
  Rect drawBannerStrip(const GfxRenderer& renderer, const char* message) const;
  virtual Rect drawPopup(const GfxRenderer& renderer, const char* message) const;
  // leftAlign left-aligns the rows instead of centring them, so a caller whose labels carry
  // a status marker keeps that marker in a fixed column rather than letting it shunt each
  // label sideways. Text is 1-bit on this panel, so an unavailable row is marked in the
  // label the caller supplies, not by the painter. Defaults to the centred look every
  // other caller in the firmware uses.
  virtual void drawOptionPopup(const GfxRenderer& renderer, const char* title, const std::vector<std::string>& options,
                               int selectedIndex, bool leftAlign = false) const;
  virtual void fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, const int progress) const;
  // v2 status bar: per-item, six-anchor layout with reflow (see StatusBar.h). Reads
  // the sb* settings and pulls battery/clock from the HAL; the reader supplies the
  // book/chapter data. Draws top and/or bottom bands plus edge progress bars.
  void drawStatusBarV2(GfxRenderer& renderer, const StatusBarData& data) const;
  void drawHelpText(const GfxRenderer& renderer, Rect rect, const char* label) const;
  virtual void drawTextField(const GfxRenderer& renderer, Rect rect, const int textWidth, bool cursorMode = false,
                             int contentStartX = 0, int contentWidth = 0) const;
  virtual bool showsFileIcons() const { return false; }

  // Shared constants and helpers for battery drawing (used by all themes)
  static constexpr int batteryPercentSpacing = 4;
  // Gap between the battery icon and the right edge of the header.
  static constexpr int batteryRightPadding = 12;
  // Font of the battery percentage. It shares a row with the version string, the
  // Pages tile and the clock on the home header, and with every other segment in
  // the reader status bar, so it must be the same size as those: UI_10.
  static constexpr int batteryPercentFontId = UI_10_FONT_ID;

  // Width of the whole right-hand battery cluster (right padding + icon + spacing +
  // a full-width "100%"). Callers use it to reserve space, to clear the previous
  // draw, and to place whatever sits to its left, so all three agree by construction.
  static int batteryClusterWidth(const GfxRenderer& renderer);
  // Top of the battery icon for a cluster whose text is drawn at rect.y: centres the
  // icon in that text's line box so icon and percentage read as one row.
  static int batteryIconTop(const GfxRenderer& renderer, const Rect& rect, int fontId);
  static void drawBatteryOutline(const GfxRenderer& renderer, int x, int y, int battWidth, int rectHeight);
  static void drawBatteryLightningBolt(const GfxRenderer& renderer, int boltX, int boltY);

 protected:
  // Index of the leftmost tab the bar draws. Zero while every label fits; once they do
  // not, the bar scrolls so the selected tab is the one guaranteed to be readable.
  // drawTabBar and tabIndexFromPoint both go through this, and apply the same
  // right-edge cut-off, so what is on screen and what answers to a touch cannot
  // disagree. A theme that overrides either of those owns keeping that true.
  size_t firstVisibleTab(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs) const;
};
