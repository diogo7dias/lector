#pragma once
#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

// Read-only "Book Info" screen: cover thumbnail, title, author, language and a
// paged plain-text synopsis (from the EPUB's dc:description). Up/Down page the
// description, Back leaves. Launched from the reader menu.
class BookInfoActivity final : public Activity {
 public:
  explicit BookInfoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                            std::string author, std::string language, std::string description, std::string coverBmpPath)
      : Activity("BookInfo", renderer, mappedInput),
        title(std::move(title)),
        author(std::move(author)),
        language(std::move(language)),
        description(std::move(description)),
        coverBmpPath(std::move(coverBmpPath)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string title;
  std::string author;
  std::string language;
  std::string description;
  std::string coverBmpPath;  // empty when no cover art is available

  bool hasCover = false;

  // Description wrapped to the content width; re-wrapped only when that width changes.
  std::vector<std::string> descLines;
  int wrapWidth = -1;
  int pageIndex = 0;
  int pageCount = 1;  // recomputed each render from the wrapped-line count and page height

  ButtonNavigator buttonNavigator;
};
