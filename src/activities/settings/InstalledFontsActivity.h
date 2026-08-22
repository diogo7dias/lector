#pragma once

#include <GfxRenderer.h>

#include <string>
#include <vector>

#include "FontInstaller.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class MappedInputManager;
struct ActivityResult;

/**
 * The fonts that are on the card, and what can be done with them without a
 * network: delete one, or send one to another reader.
 *
 * The font download screen next door lists what the font server offers, which
 * means it needs WiFi to show anything and cannot see a font that was copied
 * onto the card by hand or arrived from another reader. This screen reads the
 * card instead, so it works with the radio off and shows every family that is
 * actually installed.
 */
class InstalledFontsActivity final : public Activity {
 public:
  explicit InstalledFontsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  /** One installed family, with the figures the list shows beside its name. */
  struct Family {
    std::string name;
    std::vector<std::string> facePaths;
    uint8_t sizeCount = 0;
    uint64_t totalBytes = 0;
    /** True while the reader is set to read in this font. */
    bool inUse = false;
    /** False when the family cannot travel: the offer format cannot name it. */
    bool sendable = true;
  };

  /** The row list, or the two things that can be done to the row picked. */
  enum class View : uint8_t { Families, Actions };

  /** Actions offered for the family picked, in the order they are drawn. */
  enum class Action : uint8_t { Send, Delete, Count };

  void loadFamilies();
  void loopFamilies();
  void loopActions();
  void promptDelete();
  void onDeleteConfirmed(const ActivityResult& result);
  void sendSelectedFamily();
  const Family* selectedFamily() const;
  int itemCount() const;
  std::string sizeLine(const Family& family) const;

  std::vector<Family> families;
  FontInstaller fontInstaller;
  View view = View::Families;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  Action selectedAction = Action::Send;
  std::string errorMessage;
};
