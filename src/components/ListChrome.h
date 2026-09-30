#pragma once

#include <array>

#include "ListChromeLayout.h"

class GfxRenderer;
class MappedInputManager;

// What a screen puts around its body, as data. The base paints it, so a screen
// that wants a counter beside its title, a hint line under it, or a footnote
// above the button hints no longer overrides the paint to get one — which is
// how those screens ended up drawing their own headers and drifting away from
// the theme.
struct ListChrome {
  // Title band. nullptr draws no band, and the body starts at the top of the
  // screen; an empty string draws the band with its battery cluster but no
  // text. A long title wraps and the band grows with it.
  const char* title = nullptr;
  // Right of the title, in the band, for a count the screen keeps ("3 / 8").
  const char* headerRight = nullptr;
  // Bottom right, above the button hints (the firmware version on Settings).
  const char* footerRight = nullptr;
  // A band under the title for a line about the screen rather than about any
  // row (what the middle button does here).
  const char* subHeader = nullptr;
  const char* subHeaderRight = nullptr;
  // Centred lines under the bands, for a screen whose header is a block rather
  // than a line: the reader menu names the book, its author, the chapter and
  // how far in the reader is. Each entry is one logical line; the painter wraps
  // it over as many screen lines as it needs and reserves them all.
  static constexpr int MAX_HEADER_LINES = 8;
  std::array<const char*, MAX_HEADER_LINES> headerLines{};
  // The contents look's header: the book set like a title page (title in tracked
  // capitals, author in italic, a short rule, the chapter in small capitals, the
  // progress in italic), in place of headerLines. Starts at the top of the panel.
  struct TitlePage {
    const char* title = nullptr;
    const char* author = nullptr;
    const char* chapter = nullptr;
    const char* progress = nullptr;
  } titlePage;
  // False drops the button-hint band, and the body runs to the panel's foot.
  bool hints = true;
  // Set by toContentsLook(): footnotes are set in italic, centred, over an 18px foot.
  bool contents = false;
  // The contents look's folio: one small-caps line at the foot, under any footnotes, for
  // what the device says about itself (the clock and the battery on Home).
  const char* folio = nullptr;
  // Keeps the hint band in the contents look, for hints that say what the rows cannot
  // (Remap Front Buttons previews the mapping on the keys themselves).
  bool contentsKeepsHints = false;
  // A centred note under everything above, wrapped like the header lines.
  const char* note = nullptr;
  // Lines above the button hints, for something true of the whole list rather
  // than of the selection: what a hold does, what the side buttons do, or a
  // warning the screen wants under the rows instead of over them.
  static constexpr int MAX_FOOTNOTES = 2;
  std::array<const char*, MAX_FOOTNOTES> footnotes{};
  // Button hints. nullptr takes the default for that slot; an empty string
  // leaves it blank, which is how a screen says that button does nothing.
  const char* backHint = nullptr;
  const char* confirmHint = nullptr;
  const char* thirdHint = nullptr;
  const char* fourthHint = nullptr;
  // Side inset for the body. Lists usually run edge to edge; a screen with
  // prose in its rows asks for the theme's content padding.
  int sideInset = 0;
};

// The contents look for a screen that describes its chrome the classic way: the title
// and one line about the screen (sub-header, counter or note, the first there is) become
// a title page, and the hint band goes unless the board is touch, where it is the only
// Back. A chrome that already set its own titlePage keeps it.
void toContentsLook(ListChrome& chrome, bool keepHints);

// The bands, measured from the live renderer and theme.
list_chrome::Bands listChromeBands(const GfxRenderer& renderer, const ListChrome& chrome);

// Paints the bands that sit above the body. Called before the app renders.
void drawListChromeTop(const GfxRenderer& renderer, const ListChrome& chrome);

// Paints the footnote and the button hints. Called after the app renders.
void drawListChromeBottom(GfxRenderer& renderer, const MappedInputManager& mappedInput, const ListChrome& chrome);
