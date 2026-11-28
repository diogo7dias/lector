#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "util/EndOfBookSuggestions.h"

class GfxRenderer;
struct Rect;
class MappedInputManager;

// Shared End-of-Book screen for the EPUB, TXT, and XTC readers. Collects up to
// MAX_SUGGESTIONS next-book suggestions (folder siblings, falling back to
// LibraryIndexFile when the book was filed into /recents or has no siblings) once per
// reader session, handles the menu input, and always renders action rows (Sync Progress
// when configured, View Quotes when present, Home) plus button hints.
class EndOfBookOptions {
 public:
  enum class Action { None, Redraw, OpenBook, SyncProgress, ViewQuotes, GoHome, LastPage };

  static constexpr size_t MAX_SUGGESTIONS = end_of_book::MAX_SUGGESTIONS;

  struct Suggestion {
    std::string displayName;
    std::string fullPath;
  };

  // Scans for next-book suggestions and gathers what the finished page says about
  // the book (author, the date it was finished and the last saved quote); no-op when
  // already loaded. Call ONLY from the reader's render() (the render task, serialized by
  // RenderLock): the loaded flag is the release/acquire publication point that lets the
  // main task read the finished list safely.
  void loadOnce(const std::string& currentBookPath, const std::string& title, const std::string& author,
                bool canSync = false, bool allowQuotes = false);

  // True once loaded: the end screen always offers action rows and button hints.
  bool menuActive() const;

  // Menu input handling, following the standard list idiom: side Up/Down and front
  // Left/Right move the selection (wrapping), Confirm opens it (or runs the action),
  // and a short Back press returns to the last page of the book. Fills openPath when
  // the result is OpenBook. Returns Action::None when nothing relevant was pressed;
  // callers continue their normal input path (keeping long-press Back working).
  Action handleMenuInput(const MappedInputManager& input, std::string* openPath);

  // Draws the full end screen (finished summary, suggestions/actions, and button hints)
  // onto a cleared buffer.
  void render(GfxRenderer& renderer, const MappedInputManager& input) const;

 private:
  // Written by the render task in loadOnce(), immutable afterwards; the main task only
  // reads these after isLoaded is observed true (acquire), so no further locking is needed.
  std::vector<Suggestion> suggestions;
  end_of_book::ActionRows rows;
  int selector = 0;
  // First row of the last drawn window, fed back by drawWrappedList. Written by the
  // render pass, which is why it is mutable behind a const render().
  mutable int scrollOffset = 0;
  std::atomic<bool> isLoaded{false};

  // The finished page, filled by loadOnce and read only by render (both on the render task).
  std::string bookTitle;
  std::string author;
  std::string quote;
  char datesLine[64] = "";

  Action activateRow(int rowIndex, std::string* openPath) const;
  // Draws the book's lines from y down inside `area`; returns the y below them.
  int drawFinished(const GfxRenderer& renderer, const Rect& area, int y) const;
};
