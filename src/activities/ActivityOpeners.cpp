#include <Logging.h>
#include <Memory.h>

#include "ActivityManager.h"
#include "home/FileBrowserActivity.h"
#include "network/CrossPointWebServerActivity.h"
#include "network/NearbyFileTransferActivity.h"
#include "settings/OpdsServerListActivity.h"
#include "settings/SettingsActivity.h"

// Back to the screen a replace destroyed (ActivityManager::Opener). The opener is rebuilt,
// not kept: these replaces exist to hand its RAM to a radio, and the screens are cheap to
// build again on the row they were left from.

void ActivityManager::replaceFromOpener(std::unique_ptr<Activity>&& newActivity, const Opener opener,
                                        std::string path) {
  if (!newActivity) {
    LOG_ERR("ACT", "OOM: no screen to open over its opener");
    return;
  }
  // ponytail: a short chain at most (Settings > File Transfer > Nearby); no cap needed.
  openers.push_back(OpenerEntry{opener, std::move(path)});
  queueReplace(std::move(newActivity));
}

bool ActivityManager::returnsToSettings() const {
  if (openers.empty()) return false;
  const Opener last = openers.back().opener;
  return last == Opener::SettingsFileTransfer || last == Opener::SettingsShareCredentials;
}

void ActivityManager::goBackToReader(std::string path) {
  const MenuReturn menu = readerMenu;
  goToReader(std::move(path));  // forgets it: only Back brings the menu back
  readerMenu = menu;
}

bool ActivityManager::reopenOpener() {
  if (openers.empty()) return false;
  OpenerEntry entry = std::move(openers.back());
  openers.pop_back();
  std::unique_ptr<Activity> screen;
  switch (entry.opener) {
    case Opener::SettingsFileTransfer:
      screen = SettingsActivity::onFileTransferCard(renderer, mappedInput);
      break;
    case Opener::SettingsShareCredentials:
      screen = SettingsActivity::onShareCredentials(renderer, mappedInput);
      break;
    case Opener::FileBrowser:
      screen = makeUniqueNoThrow<FileBrowserActivity>(renderer, mappedInput, std::move(entry.path));
      break;
    case Opener::NearbyChoose:
      screen = makeUniqueNoThrow<NearbyFileTransferActivity>(renderer, mappedInput,
                                                             NearbyFileTransferActivity::Mode::Choose);
      break;
    case Opener::FileTransfer:
      screen = makeUniqueNoThrow<CrossPointWebServerActivity>(renderer, mappedInput);
      break;
    case Opener::OpdsServers:
      screen = makeUniqueNoThrow<OpdsServerListActivity>(renderer, mappedInput, true);
      break;
  }
  if (!screen) {
    LOG_ERR("ACT", "OOM: could not rebuild the opener, going home");
    return false;
  }
  queueReplace(std::move(screen));
  return true;
}
