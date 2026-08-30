#pragma once

#include <array>

#include "activities/Activity.h"
#include "components/UiAppHost.h"

// Base for the screens that report on something rather than list it: a clear, a
// sweep, a sync, a firmware write. Every one of them is the same shape — a
// header, a few centred lines, sometimes a progress bar, and the hints — and
// each used to hand-roll it with drawCenteredText() at hardcoded offsets from
// the middle of the screen. That is what a look change cannot reach: the text
// sits where the arithmetic put it, in whatever font the call named.
//
// Subclasses now describe the screen (statusView()) and the base draws it
// through FreeInkUI, so the theme owns the type, the spacing and the bar.
// Screens that are a list belong on UiListActivity instead.
class UiStatusActivity : public Activity, protected UiAppHost {
 public:
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 protected:
  // The on-screen button, when a subclass offers one. Subclass actions start
  // at ACTION_USER.
  static constexpr freeink::ui::ActionId ACTION_PRIMARY = 1;
  static constexpr freeink::ui::ActionId ACTION_USER = 2;

  // At most four lines, which is what the wordiest screen (the clear-cache
  // warning) needs. The first is the headline and is drawn in the body face;
  // the rest are the smaller face under it.
  static constexpr size_t MAX_LINES = 4;

  // Screens that explain rather than report ("do this on your computer, then
  // this") are a stack of left-aligned sections instead of centred lines. A
  // view carries one shape or the other: sections win when the first one has a
  // heading.
  static constexpr size_t MAX_SECTIONS = 3;

  struct Section {
    const char* heading = nullptr;  // bold, in the body face
    std::array<const char*, MAX_LINES> lines{};
  };

  struct StatusView {
    const char* title = nullptr;  // header band; nullptr draws no header
    // Sub-header band under the title, for the network a screen is reachable
    // on. Left is the name, right the address.
    const char* subtitleLeft = nullptr;
    const char* subtitleRight = nullptr;
    std::array<const char*, MAX_LINES> lines{};
    std::array<Section, MAX_SECTIONS> sections{};
    // Drawn over the bar, for a transfer that can say what it is moving.
    const char* progressLabel = nullptr;
    // A bar under the lines. Left off, the lines centre on their own.
    bool showProgress = false;
    int progressValue = 0;
    int progressMax = 100;
    // Footer hints. An empty string leaves that slot blank, which is how a
    // working screen says no button does anything yet.
    const char* backHint = nullptr;     // nullptr = "Back"
    const char* confirmHint = nullptr;  // nullptr = blank
  };

  UiStatusActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput);

  // Re-read every render; a subclass returns whatever its state machine is in
  // the middle of.
  virtual StatusView statusView() const = 0;

  // Confirm, or a tap on the screen's own button. Nothing by default: most of
  // these screens only offer a way out.
  virtual void onConfirmButton() {}
  virtual void onBackButton() { finish(); }
  // First hook in loop(); return true when the pass is consumed (a popup, a
  // state machine's own tick).
  virtual bool handleCustomInput() { return false; }
  // Drawn after the hints, for a legacy pop-up that owns the frame. Returning
  // true means the overlay published the buffer itself.
  virtual bool drawOverlay() { return false; }

  void buildScreen(UiScreen& screen);

 private:
  // The two body shapes. Centred lines for a screen that reports on itself;
  // left-aligned sections for one that gives instructions.
  void buildCentredLines(UiScreen& screen, const StatusView& view);
  void buildSections(UiScreen& screen, const StatusView& view);
  static void drawProgress(UiScreen& screen, const StatusView& view, const freeink::ui::Rect& rect);

 private:
  static void screenTrampoline(UiScreen& screen, void* user);
};
