#pragma once

#include "components/UiAppHost.h"

class GfxRenderer;
class MappedInputManager;

// FreeInkUI chrome for the reader's toolbar (Settings > Reader > Reader Menu Style >
// Toolbar), painted over the page that is already on screen: a bottom sheet holding a
// chapter scrub row (< track >), the chapter title with page and percent, and a
// Contents / Text / More tile row. The page above the sheet is left untouched.
//
// Ported from CrossPoint 2.0.0's ReaderToolbarUi, toolbar half only: the tiles open
// lector's own chapter list, book text settings and list menu instead of second copies.
//
// EpubReaderActivity owns the state (which tile is focused) and the physical buttons;
// this class owns the layout, the tap targets FreeInkUI registers, and the translation
// of dispatched actions into the small event set the reader acts on. Nothing here
// clears the screen.
class ReaderToolbarUi : public UiAppHost {
 public:
  enum class Event { None = 0, Dismiss = 1, Tool = 2, PrevChapter = 3, NextChapter = 4, Scrub = 5 };

  struct Model {
    const char* chapterTitle = nullptr;
    const char* pageInfo = nullptr;  // "12/40   51%"
    int progressPermille = 0;        // 0..1000 book progress (the scrub knob)
    int activeTool = -1;             // the tile the buttons' cursor is on; -1 = none shown
  };

  struct Routed {
    Event event = Event::None;
    int value = 0;        // Tool: tile index
    int permille = -1;    // Scrub: 0..1000 along the track
    bool routed = false;  // the gate was open and a touch frame reached the chrome
  };

  explicit ReaderToolbarUi(GfxRenderer& renderer);

  // Screen-entry reset and action wiring. Call once when the toolbar opens.
  void begin();
  // What the next render() draws. The strings must stay alive until render() returns.
  void setModel(const Model& model) { model_ = model; }
  // Paints into the framebuffer: no clear, no refresh. Run from the render task.
  void render() { renderUi(); }
  // Routes one loop-task input frame and returns the action it mapped to, if any.
  Routed route(const MappedInputManager& input);

 private:
  static void screenFn(UiScreen& screen, void* user);
  static void onAction(const freeink::ui::ActionEvent& event, void* user);
  void buildToolbar(UiScreen& screen);

  Model model_;
  Routed pending_;
  // fui::ButtonProps embeds a 324-byte StyleSet: kept off the stack (AGENTS.md: locals stay
  // under 256 bytes).
  freeink::ui::ButtonProps stepProps_;
};
