#include <Logging.h>
#include <Memory.h>

#include "ActivityManager.h"
#include "ActivityNames.h"
#include "BackTrail.h"
#include "CrossPointState.h"
#include "home/FileBrowserActivity.h"
#include "network/CrossPointWebServerActivity.h"
#include "network/NearbyFileTransferActivity.h"
#include "settings/OpdsServerListActivity.h"
#include "settings/SettingsActivity.h"

// Back to the screen a replace destroyed (ActivityManager::Opener). The opener is rebuilt,
// not kept: these replaces exist to hand its RAM to a radio, and the screens are cheap to
// build again on the row they were left from.

HomeMenuItem homeRowFor(const std::string& activityName) {
  if (activityName == activity_name::kFileBrowser) return HomeMenuItem::FILE_BROWSER;
  if (activityName == activity_name::kLibrary) return HomeMenuItem::LIBRARY;
  if (activityName == activity_name::kOpdsBookBrowser) return HomeMenuItem::OPDS_BROWSER;
  if (activityName == activity_name::kOpdsServerList) return HomeMenuItem::OPDS_BROWSER;
  if (activityName == activity_name::kCrossPointWebServer) return HomeMenuItem::FILE_TRANSFER;
  if (activityName == activity_name::kNearbyFileTransfer) return HomeMenuItem::FILE_TRANSFER;
  if (activityName == activity_name::kSettings) return HomeMenuItem::SETTINGS_MENU;
  return HomeMenuItem::NONE;
}

void ActivityManager::replaceFromOpener(std::unique_ptr<Activity>&& newActivity, const Opener opener, std::string path,
                                        const int row) {
  if (!newActivity) {
    LOG_ERR("ACT", "OOM: no screen to open over its opener");
    return;
  }
  // ponytail: a short chain at most (Settings > File Transfer > Nearby); no cap needed.
  openers.push_back(OpenerEntry{opener, std::move(path), row});
  queueReplace(std::move(newActivity));
}

uint32_t ActivityManager::backTrail() const {
  back_trail::Step steps[back_trail::kMaxOpeners];
  const int count = static_cast<int>(std::min<size_t>(openers.size(), back_trail::kMaxOpeners));
  for (int i = 0; i < count; ++i) {
    const OpenerEntry& entry = openers[openers.size() - count + i];
    steps[i] = {static_cast<uint8_t>(entry.opener), static_cast<uint8_t>(std::max(entry.row, 0))};
  }
  const HomeMenuItem home = currentActivity ? homeRowFor(currentActivity->name) : HomeMenuItem::NONE;
  return back_trail::pack(steps, count, static_cast<uint8_t>(home));
}

// The browser's folder does not fit the word: it comes back on the card's root. No restart
// screen has the browser in its chain today (only Nearby, which never reboots).
void ActivityManager::followBackTrail(const uint32_t word) {
  const back_trail::Trail trail =
      back_trail::unpack(word, static_cast<uint8_t>(Opener::Reader), static_cast<uint8_t>(HomeMenuItem::QUICK_PANEL));
  openers.clear();
  for (int i = 0; i < trail.count; ++i) {
    openers.push_back(OpenerEntry{static_cast<Opener>(trail.steps[i].opener), {}, trail.steps[i].row});
  }
  if (!reopenOpener()) goHome(static_cast<HomeMenuItem>(trail.homeRow));
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
    case Opener::SettingsWifi:
      screen = SettingsActivity::onWifiNetworks(renderer, mappedInput);
      break;
    case Opener::FileBrowser:
      screen = makeUniqueNoThrow<FileBrowserActivity>(renderer, mappedInput, std::move(entry.path));
      break;
    case Opener::NearbyChoose:
      screen = makeUniqueNoThrow<NearbyFileTransferActivity>(renderer, mappedInput,
                                                             NearbyFileTransferActivity::Mode::Choose);
      break;
    case Opener::FileTransfer:
      screen = makeUniqueNoThrow<CrossPointWebServerActivity>(renderer, mappedInput, entry.row);
      break;
    case Opener::Reader: {
      // The book is the bottom of any chain, so goToReader() forgetting the openers loses none.
      std::string path = entry.path.empty() ? APP_STATE.openEpubPath : std::move(entry.path);
      if (path.empty()) return false;
      goToReader(std::move(path));
      return true;
    }
    case Opener::OpdsServers:
      screen = makeUniqueNoThrow<OpdsServerListActivity>(renderer, mappedInput, true, entry.row);
      break;
  }
  if (!screen) {
    LOG_ERR("ACT", "OOM: could not rebuild the opener, going home");
    return false;
  }
  queueReplace(std::move(screen));
  return true;
}
