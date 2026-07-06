#pragma once
#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

// Browses the saved quotes in a <book>_QUOTES.txt sidecar file.
// Up/Down to scroll, hold Confirm to delete, Back to leave.
class QuotesViewerActivity final : public Activity {
 public:
  struct QuoteEntry {
    std::string chapter;
    std::string text;
  };

  explicit QuotesViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                const std::string& quotesFilePath)
      : Activity("QuotesViewer", renderer, mappedInput), filePath(quotesFilePath) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string filePath;
  std::string bookTitle;
  std::vector<QuoteEntry> quotes;
  int selectorIndex = 0;
  int confirmingDelete = 0;  // 0 = hidden, 1 = show dialog, 2 = confirmed-arm
  ButtonNavigator buttonNavigator;

  void loadQuotes();
  bool saveQuotes() const;
  int getGutterBottom(const GfxRenderer& renderer) const;
  int getListHeight(const GfxRenderer& renderer) const;
  static std::string deriveBookTitle(const std::string& path);
};
