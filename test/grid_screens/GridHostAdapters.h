#pragma once

#include <FreeInkApp.h>

#include <cstring>
#include <vector>

#include "ListSwipeGesture.h"
#include "components/ListChrome.h"
#include "components/ListScrollbar.h"
#include "components/SettingsGrid.h"
#include "components/TwoTapGate.h"
#include "components/UiRowHeight.h"
#include "components/UiRowWrap.h"
#include "components/WrappedListWindow.h"

namespace fui = freeink::ui;
using Rect = list_chrome::Rect;

// Hardware, chrome, and task scheduling only. The generated includes below keep
// the production UiGridActivity, UiAppHost, gate, theme, and SDK drawing/routing.
struct HalDisplay {
  enum RefreshMode { FAST_REFRESH };
  struct Profile {
    bool isX4Pro = true;
  } device;
  const Profile& profile() const { return device; }
};
inline HalDisplay display;

struct GfxRenderer {
  int getScreenWidth() const { return 480; }
  int getScreenHeight() const { return 800; }
  void clearScreen() {}
  void displayBuffer() { ++refreshes; }
  int refreshes = 0;
};

namespace freeink::ui {
class GfxRendererTarget : public DrawTarget {
 public:
  static constexpr FontId FONT_BODY = 1;
  struct Fill {
    Rect rect;
    Paint paint;
  };
  struct Stroke {
    Rect rect;
    Paint paint;
    uint8_t width;
  };
  std::vector<Fill> fills;
  std::vector<Stroke> strokes;
  std::vector<TextStyle> texts;
  explicit GfxRendererTarget(const GfxRenderer&) {}
  DeviceContext deviceContext() const {
    DeviceContext device;
    device.width = 480;
    device.height = 800;
    return device;
  }
  Size measureText(FontId, const char* text, TextStyle) const override {
    return {static_cast<int16_t>(std::strlen(text) * 6), 12};
  }
  int16_t lineHeight(FontId) const override { return 12; }
  void fill(Rect rect, Paint paint, uint8_t, uint8_t) override { fills.push_back({rect, paint}); }
  void stroke(Rect rect, Paint paint, uint8_t width, uint8_t, uint8_t) override {
    strokes.push_back({rect, paint, width});
  }
  void text(Rect, const char*, TextStyle style) override { texts.push_back(style); }
  void line(Point, Point, uint8_t, Paint) override {}
  void triangle(Point, Point, Point, Paint) override {}
  void bitmap(Rect, BitmapRef, BitmapMode, Paint, Rotation) override {}
};
}  // namespace freeink::ui

struct MappedInputManager {
  enum class Button { Confirm, Back, ScreenDown, ScreenUp, ScreenLeft, ScreenRight };
  bool touch = true;
  bool key = false;
  fui::InputSnapshot snapshot;
  bool hasTouch() const { return touch; }
  bool wasPressed(Button) const { return false; }
  bool wasAnyPressed() const { return key; }
  int tappedHintHardware() const { return -1; }
  list_swipe::Scroll wasListScrollSwipe() const { return list_swipe::Scroll::None; }
};

struct RenderLock {
  template <class T>
  explicit RenderLock(T&) {}
};
struct Activity {
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  Activity(const char*, GfxRenderer& renderer, MappedInputManager& input) : renderer(renderer), mappedInput(input) {}
  virtual ~Activity() = default;
  virtual void onEnter() {}
  virtual void loop() {}
  virtual void render(RenderLock&&) {}
  void requestUpdate() {}
  void finish() {}
};
struct ButtonNavigator {
  void resetRowTap() {}
  template <class F>
  void onRowTap(MappedInputManager::Button, F) {}
  template <class F>
  void onContinuous(std::initializer_list<MappedInputManager::Button>, F) {}
  template <class F>
  void onPressAndContinuous(std::initializer_list<MappedInputManager::Button>, F) {}
};

struct ThemeMetrics {
  int listRowGap = 4, listRowRadius = 0, listInset = 0, listSidePadding = 0;
  int listSelectionStyle = 0, listScrollWidth = 0, listScrollSide = 0;
  int headerHeight = 40, headerSidePadding = 0, headerUnderlineSize = 0, headerTitleAlign = 0;
  int controlRadius = 0, sheetRadius = 0, capsuleRadius = 0;
  int listRowHeight = 40, verticalSpacing = 10;
};
struct UITheme {
  static UITheme& getInstance() {
    static UITheme theme;
    return theme;
  }
  const ThemeMetrics& getMetrics() const {
    static ThemeMetrics metrics;
    return metrics;
  }
  void drawScrollArrows(GfxRenderer&, Rect, list_scrollbar::Arrows) {}
};
#define GUI UITheme::getInstance()
namespace BoardConfig {
inline const struct {
  struct {
    int left = 0, right = 0;
  } viewableInsets;
} ACTIVE;
}  // namespace BoardConfig

#include "UIThemeTokens.h.inc"
#include "UiAppHost.h.inc"

inline std::atomic<const fui::ThemeTokens*>& sharedUiThemeCell() {
  static std::atomic<const fui::ThemeTokens*> cell{nullptr};
  return cell;
}
inline fui::GfxRendererTarget makeUiTarget(const GfxRenderer& renderer) { return fui::GfxRendererTarget(renderer); }
inline void applySharedUiTheme(GatedApp& app, const fui::GfxRendererTarget& target) {
  static fui::ThemeTokens theme;
  theme = uiThemeTokens(target);
  sharedUiThemeCell().store(&theme);
  app.setThemeRef(&sharedUiThemeCell());
}
inline fui::InputSnapshot touchSnapshotFrom(const MappedInputManager& input, bool) { return input.snapshot; }
inline list_chrome::Bands listChromeBands(const GfxRenderer&, const ListChrome&) {
  list_chrome::Bands bands;
  bands.contentBottom = 800;
  return bands;
}
inline void drawListChromeTop(const GfxRenderer&, const ListChrome&) {}
inline void drawListChromeBottom(GfxRenderer&, const MappedInputManager&, const ListChrome&) {}

// clang-format off: declarations must precede the production definitions.
#include "UiGridActivity.h.inc"
#include "UiAppHost.cpp.inc"
#include "UiGridActivity.cpp.inc"
// clang-format on
