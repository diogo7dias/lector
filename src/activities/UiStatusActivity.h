#pragma once

#include <HalDisplay.h>

#include <array>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"
#include "components/themes/BaseTheme.h"  // Rect, for the QR squares the body layout places

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
  static constexpr freeink::ui::ActionId ACTION_ACCEPT = 1;
  static constexpr freeink::ui::ActionId ACTION_CANCEL = 2;
  // A row of the comparison shape's choice list.
  static constexpr freeink::ui::ActionId ACTION_CHOICE = 3;
  // A row of the scrolling list shape.
  static constexpr freeink::ui::ActionId ACTION_LIST = 4;
  // One id for the whole slider row: a drag arrives with dragPermille set, a
  // step button with its delta in the event's value.
  static constexpr freeink::ui::ActionId ACTION_SLIDER = 5;
  static constexpr freeink::ui::ActionId ACTION_USER = 6;

  // At most four lines, which is what the wordiest screen (the clear-cache
  // warning) needs. The first is the headline and is drawn in the body face;
  // the rest are the smaller face under it.
  static constexpr size_t MAX_LINES = 4;

  // Screens that explain rather than report ("do this on your computer, then
  // this") are a stack of left-aligned sections instead of centred lines. A
  // view carries one shape or the other: sections win when the first one has a
  // heading.
  static constexpr size_t MAX_SECTIONS = 3;

  // The two sync screens compare a position held here with one held elsewhere,
  // then offer the same two answers: take theirs, or send ours. Detail lines
  // per side: the chapter, the page, and the device the other one came from.
  static constexpr size_t MAX_SIDE_LINES = 3;
  static constexpr size_t MAX_CHOICES = 2;
  // A data-driven list of answers (the readers found nearby) is bounded here;
  // ESP-NOW discovery never lists more than a handful at once.
  static constexpr size_t MAX_LIST_CHOICES = 8;

  struct Section {
    const char* heading = nullptr;  // bold, in the body face
    std::array<const char*, MAX_LINES> lines{};
    // A block of prose the base wraps to the section's width, for a screen
    // whose text is a report rather than a set of prepared lines (the crash
    // reason, and the sentence explaining it). Used instead of `lines`.
    const char* paragraph = nullptr;
    int paragraphMaxLines = 4;
    // A QR square drawn at the left of the section, with the lines beside it
    // instead of under it. This is what a phone is pointed at, so it is part of
    // the section rather than a separate block.
    const char* qrPayload = nullptr;
  };

  struct ComparisonSide {
    const char* label = nullptr;  // bold, in the body face
    std::array<const char*, MAX_SIDE_LINES> lines{};
  };

  struct StatusView {
    // A state whose screen belongs to a subactivity: the base leaves the buffer
    // exactly as it found it.
    bool hidden = false;
    const char* title = nullptr;  // header band; nullptr draws no header
    // Right of the title, for a count the screen keeps ("12 networks").
    const char* headerRight = nullptr;
    // Sub-header band under the title, for the network a screen is reachable
    // on. Left is the name, right the address.
    const char* subtitleLeft = nullptr;
    const char* subtitleRight = nullptr;
    std::array<const char*, MAX_LINES> lines{};
    std::array<Section, MAX_SECTIONS> sections{};
    // Drawn over the bar, for a transfer that can say what it is moving.
    const char* progressLabel = nullptr;
    // Centred shape only: a code under the lines, with its own lines (the
    // address it carries) under that.
    const char* qrPayload = nullptr;
    std::array<const char*, MAX_LINES> qrLines{};
    // Link strength at the right of the sub-header band. Off unless the screen
    // sets showSignal; bars run 0 to 4, and a screen with no link asks for the
    // cross by leaving connected false.
    bool showSignal = false;
    bool signalConnected = false;
    int signalBars = 0;
    // A slider band under the first line, for a screen whose whole job is
    // picking one number (go-to-percent, every interval). The caption carries
    // the label on the left and the readout on the right; the lines after the
    // first sit under the band, which is where the step hints belong.
    bool showSlider = false;
    const char* sliderLabel = nullptr;
    const char* sliderValueText = nullptr;
    int sliderValue = 0;
    int sliderMin = 0;
    int sliderMax = 100;
    // A bar under the lines. Left off, the lines centre on their own.
    bool showProgress = false;
    int progressValue = 0;
    int progressMax = 100;
    // Footer hints. An empty string leaves that slot blank, which is how a
    // working screen says no button does anything yet.
    const char* backHint = nullptr;     // nullptr = "Back"
    const char* confirmHint = nullptr;  // nullptr = blank
    // The other two buttons, for a screen that binds them (the Wi-Fi picker
    // forgets a network and rescans). Blank unless set.
    const char* thirdHint = nullptr;
    const char* fourthHint = nullptr;
    // On-screen buttons across the foot of the body, for a choice a touch
    // reader should not have to find on the hint band. They are the same two
    // answers the keys give: the left one calls onBackButton(), the right one
    // onConfirmButton().
    const char* cancelLabel = nullptr;
    const char* acceptLabel = nullptr;
    // Comparison shape: a headline, the two sides, a line naming which is
    // ahead, and the choices as a list at the foot of the body. It wins over
    // both other shapes when the first side carries a label.
    const char* comparisonHeadline = nullptr;
    std::array<ComparisonSide, 2> comparison{};
    const char* comparisonRelation = nullptr;
    std::array<const char*, MAX_CHOICES> choices{};
    // Answers that are data rather than a fixed pair (the readers found
    // nearby). Points at storage the subclass owns: the view itself is a
    // temporary, so it cannot carry the strings. Used when `choices` is empty,
    // and it needs no comparison above it — a screen can pair a list of answers
    // with plain centred lines.
    const char* const* choiceList = nullptr;
    int choiceListCount = 0;
    // A scrolling list filling the body, for a screen that is a list in one
    // state and a message in the others: the Wi-Fi picker and the font
    // browser, which is why neither could live on UiListActivity. The rows are
    // the subclass's, since the view is a temporary.
    const freeink::ui::ListItem* listItems = nullptr;
    int listCount = 0;
    bool listHasSubtitle = false;
    // A line under the list, for the legend the Wi-Fi picker prints.
    const char* listNote = nullptr;
    // The centred code's side, when the default 198 px is not what the screen
    // wants. A screen whose whole point is the code (Display QR) asks for the
    // largest square its body holds; 0 keeps the default.
    int qrSize = 0;
    // A screen that is nearly all QR asks for a full pass: a differential
    // waveform leaves the old pattern as speckle under a dense block of black.
    HalDisplay::RefreshMode refresh = HalDisplay::FAST_REFRESH;
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
  // Drawn straight after the header band, for a screen with a mark of its own
  // there (the OPDS browser's search glyph). The band's rect is passed so the
  // mark follows the theme's header height.
  virtual void drawHeaderExtras(const Rect& headerRect) {}
  virtual bool drawOverlay() { return false; }
  // Called once the page is on the panel, for a screen that tracks what the
  // panel is showing (a full pass is owed when the screen changes wholesale,
  // and not owed for the tick of a progress bar).
  virtual void afterRender() {}

  // The comparison shape's selected choice, and what a press on it does. The
  // base owns the index so both sync screens navigate identically; a subclass
  // only says what taking each answer means.
  // The scrolling list's selection, and what activating a row does. The base
  // owns the selection and the viewport, so a screen only says what its rows
  // are and what choosing one means.
  int listSelection() const { return listNav_.selected; }
  void setListSelection(int index);
  // Same, for a caller already holding the render lock: the lock is a plain
  // mutex, so taking it twice on one task hangs the reader.
  void setListSelectionLocked(int index);
  virtual void onListActivated(int index) {}

  int choiceIndex() const { return choiceIndex_; }
  // Opening choice, for a screen that knows which answer is the likely one (the
  // side that is further along). Clamped again at build time.
  void setChoiceIndex(int index) { choiceIndex_ = index < 0 ? 0 : index; }
  virtual void onChoiceActivated(int index) {}
  // The slider moved: `value` is already inside [sliderMin, sliderMax].
  virtual void onSliderChanged(int value) {}
  // What the on-screen minus and plus buttons move by. The keys keep their own
  // small and large steps; this is only the touch pair.
  virtual int sliderStep() const { return 1; }

  void buildScreen(UiScreen& screen);

 private:
  // The two body shapes. Centred lines for a screen that reports on itself;
  // left-aligned sections for one that gives instructions.
  void buildCentredLines(UiScreen& screen, const StatusView& view);
  void buildSections(UiScreen& screen, const StatusView& view);
  void buildComparison(UiScreen& screen, const StatusView& view);
  // The answers, taken from the bottom of the body so whichever shape is drawn
  // above them cannot run underneath. Sets choiceCount_.
  void buildChoiceBand(UiScreen& screen, const StatusView& view);
  void buildList(UiScreen& screen, const StatusView& view);
  void buildSlider(UiScreen& screen, const StatusView& view, const freeink::ui::Rect& rect);
  // Selection and viewport for the list shape, plus the repeat behaviour that
  // steps a row on a press and a page on a hold.
  freeink::ui::ListNav listNav_;
  ButtonNavigator listButtons_;
  int listCount_ = 0;
  bool navigateList();
  // Choices the last build drew, so the loop task can step the selection
  // without rebuilding the view. Zero until the first render, which is also
  // when there is nothing on screen to step through.
  int choiceCount_ = 0;
  // The rows the last build drew. They outlive the build because FreeInkUI's
  // interaction table points at them.
  std::array<freeink::ui::ListItem, MAX_LIST_CHOICES> choiceItems_{};
  int choiceIndex_ = 0;
  bool moveChoice(int delta);
  // QR codes are bitmaps rather than FreeInkUI elements, so the body layout
  // records where they go and render() paints them into the same buffer once
  // the app has drawn the text around them. Cleared at the start of every
  // build, so a state without a code never inherits the last one's square.
  struct QrPlacement {
    Rect rect{};
    const char* payload = nullptr;
  };
  std::array<QrPlacement, MAX_SECTIONS> qrPlacements_{};
  void placeQr(size_t index, const Rect& rect, const char* payload);
  void drawQrCodes() const;
  // Link strength at the right of the sub-header band, or a cross when the link
  // is down.
  void drawSignal(const StatusView& view, int bandRight, int bandBottom) const;
  static void drawProgress(UiScreen& screen, const StatusView& view, const freeink::ui::Rect& rect);

  void buildActions(UiScreen& screen, const StatusView& view);

  static void screenTrampoline(UiScreen& screen, void* user);
  static void acceptTrampoline(const freeink::ui::ActionEvent& event, void* user);
  static void cancelTrampoline(const freeink::ui::ActionEvent& event, void* user);
  static void choiceTrampoline(const freeink::ui::ActionEvent& event, void* user);
  static void listTrampoline(const freeink::ui::ActionEvent& event, void* user);
  static void sliderTrampoline(const freeink::ui::ActionEvent& event, void* user);

  // Set by the last build: a screen with a capsule needs the held contact
  // routed every frame, which no other shape wants.
  bool hasSlider_ = false;
};
