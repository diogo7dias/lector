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
  // text, which is what a screen naming the book below the band wants.
  const char* title = nullptr;
  // Right of the title, for a count the screen keeps ("3 / 8").
  const char* headerRight = nullptr;
  // A band under the title for a line about the screen rather than about any
  // row (what the middle button does here).
  const char* subHeader = nullptr;
  const char* subHeaderRight = nullptr;
  // Centred lines under the bands, for a screen whose header is a block rather
  // than a line: the reader menu names the book, its author, the chapter and
  // how far in the reader is.
  // Eight, because a wrapped book title alone can take five of them.
  static constexpr int MAX_HEADER_LINES = 8;
  std::array<const char*, MAX_HEADER_LINES> headerLines{};
  // A left-aligned note under everything above, in the small face.
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

// The bands, measured from the live renderer and theme.
list_chrome::Bands listChromeBands(const GfxRenderer& renderer, const ListChrome& chrome);

// Paints the bands that sit above the body. Called before the app renders.
void drawListChromeTop(const GfxRenderer& renderer, const ListChrome& chrome);

// Paints the footnote and the button hints. Called after the app renders.
void drawListChromeBottom(GfxRenderer& renderer, const MappedInputManager& mappedInput,
                          const ListChrome& chrome);
