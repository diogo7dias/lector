#pragma once
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// List picker for the "Steal Look" reader action. Lists other recently-read
// books that carry a custom reader-settings override (a reader_override.bin
// sidecar), and returns the chosen book's cache path (FilePathResult) so the
// reader can copy that book's ReaderPrefs onto the current book. Books on the
// global default (no override) are not listed; the current book is excluded.
// Back cancels with no selection.
class StealLookActivity final : public Activity {
 public:
  StealLookActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string currentBookPath)
      : Activity("StealLook", renderer, mappedInput), currentBookPath(std::move(currentBookPath)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct Candidate {
    std::string title;
    std::string path;       // for the row icon
    std::string cachePath;  // returned to the reader; holds reader_override.bin
  };

  std::string currentBookPath;
  std::vector<Candidate> candidates;
  size_t selectorIndex = 0;
  ButtonNavigator buttonNavigator;

  void loadCandidates();
};
