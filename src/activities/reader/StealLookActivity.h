#pragma once
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// List picker for the "Steal Look" reader action. Lists other recently-read books
// that carry a custom reader-settings override (a reader_override.bin sidecar), and
// returns the chosen book's cache path (FilePathResult) so the reader can copy that
// book's ReaderPrefs onto the current book. Books still on the global default have
// no override and are not listed; the current book is excluded. Back cancels.
class StealLookActivity final : public UiListActivity {
 public:
  StealLookActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string currentBookPath)
      : UiListActivity("StealLook", renderer, mappedInput), currentBookPath(std::move(currentBookPath)) {}

  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override { return static_cast<int>(candidates.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override;

 private:
  struct Candidate {
    std::string title;
    std::string path;       // for the row icon
    std::string cachePath;  // returned to the reader; holds reader_override.bin
  };

  std::string currentBookPath;
  std::vector<Candidate> candidates;
  // Row views over `candidates`; rebuilt per render, borrowing its strings.
  std::vector<freeink::ui::ListItem> rows;

  void loadCandidates();
};
