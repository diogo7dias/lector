#pragma once
#include <cstdint>

#include "activities/UiStatusActivity.h"

// What's New: the notes for this release, shown once over Home after an update and
// reopened from Settings > System. A card that has never held state (a fresh install)
// starts with the notes marked seen, since nothing on it is new to its reader.
class ReleaseNotesActivity final : public UiStatusActivity {
 public:
  // Bump with each release whose notes change, and rewrite the notes strings in english.yaml:
  // every reader whose saved revision is lower sees What's New once.
  static constexpr uint16_t REVISION = 6;
  // The release the notes describe, kept apart from CROSSPOINT_VERSION so a patch
  // build does not relabel them.
  static constexpr char VERSION[] = "0.57.0";

  explicit ReleaseNotesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiStatusActivity("ReleaseNotes", renderer, mappedInput) {}
  void onEnter() override;

 protected:
  StatusView statusView() const override;
  void onConfirmButton() override { finish(); }
};
