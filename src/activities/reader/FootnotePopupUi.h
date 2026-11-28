#pragma once

#include <cstdint>

#include "components/UiAppHost.h"

class GfxRenderer;
class MappedInputManager;

// Bottom-sheet pop-up card for footnote text, painted straight over the live
// reading page without tearing down the chapter (reuses ReaderToolbarUi's
// FreeInkUI sheet pattern). Tap outside the card or press Back to dismiss;
// long or cross-chapter notes offer paging and a "Jump to Note" button.
class FootnotePopupUi : public UiAppHost {
 public:
  enum class Event : uint8_t { None = 0, Dismiss = 1, Jump = 2, PrevPage = 3, NextPage = 4 };

  struct Model {
    const char* title = nullptr;
    const char* pageInfo = nullptr;
    const char* const* lines = nullptr;
    int lineCount = 0;
    // Fixed line-slot budget so multi-page notes keep a steady sheet height.
    int reservedLines = 0;
    bool showPager = false;
    bool hasPrev = false;
    bool hasNext = false;
    bool showJump = false;
    const char* jumpLabel = nullptr;
  };

  struct Routed {
    Event event = Event::None;
    bool routed = false;
  };

  explicit FootnotePopupUi(GfxRenderer& renderer);

  void begin();
  void setModel(const Model& model) { model_ = model; }
  void render() { renderUi(); }
  Routed route(const MappedInputManager& input);

  // Inner wrapping width and per-page body line count matching buildSheet().
  int bodyWidth(const GfxRenderer& renderer) const;
  int maxLinesPerPage(const GfxRenderer& renderer, bool hasFooter) const;

 private:
  static void screenFn(UiScreen& screen, void* user);
  static void onAction(const freeink::ui::ActionEvent& event, void* user);
  void buildSheet(UiScreen& screen);

  Model model_;
  Routed pending_;
  // fui::ButtonProps embeds a 324-byte StyleSet: kept off the stack.
  freeink::ui::ButtonProps buttonProps_;
};
