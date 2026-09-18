#include "LutLabActivity.h"

#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>

#include "CrossPointState.h"
#include "components/BusyBanner.h"
#include "util/BusyTick.h"
#include "util/TaskWatchdog.h"

namespace fui = freeink::ui;

void LutLabActivity::onEnter() {
  auto& lab = APP_STATE.lutLab;
  if (lab.wallpaper.empty() && lutlab::validWallpaper(APP_STATE.lastSleepWallpaperPath)) {
    lab.wallpaper = APP_STATE.lastSleepWallpaperPath;
    dirty = true;
  }
  UiListActivity::onEnter();
}

void LutLabActivity::onBackButton() {
  // Sleep uses main's state write. An ordinary Back saves once and keeps the
  // screen open on failure so the user gets an actionable on-screen error.
  if (dirty && !APP_STATE.saveToFile()) {
    saveFailed = true;
    requestUpdate();
    return;
  }
  app.clearTapFlash();
  finish();
}

ListChrome LutLabActivity::chrome() const {
  auto chrome = UiListActivity::chrome();
  const auto& lab = APP_STATE.lutLab;
  const auto index = lutlab::validVariant(lab.variant);
  const auto& lut = lutlab::VARIANTS[index];
  const char* kind = index == 0 ? tr(STR_LUT_CONTROL) : tr(STR_LUT_TIMING);
  const unsigned frames = index == 0 ? display.lutLabControlFrames() : lut[2];
  snprintf(variantLabel, sizeof(variantLabel), tr(STR_LUT_VARIANT_FORMAT), index, kind);
  snprintf(bytesLabel, sizeof(bytesLabel), tr(STR_LUT_BYTES_FORMAT), frames, frames | 0x80);
  chrome.title = tr(STR_LUT_LAB);
  chrome.headerLines[0] = variantLabel;
  chrome.headerLines[1] = bytesLabel;
  chrome.headerLines[2] = lab.pinned ? tr(STR_LUT_PINNED) : tr(STR_LUT_UNPINNED);
  chrome.note = lab.wallpaper.empty() ? tr(STR_LUT_NO_IMAGE) : lab.wallpaper.c_str();
  chrome.footnotes[0] = saveFailed        ? tr(STR_LUT_SAVE_FAILED)
                        : lab.imageFailed ? tr(STR_LUT_IMAGE_FAILED)
                                          : tr(STR_LUT_INSTRUCTIONS);
  return chrome;
}

void LutLabActivity::buildScreen(UiScreen& screen) {
  const auto& lab = APP_STATE.lutLab;
  const char* labels[] = {
      tr(STR_LUT_NEXT_VARIANT), tr(STR_LUT_PREV_VARIANT), lab.pinned ? tr(STR_LUT_UNPIN) : tr(STR_LUT_PIN),
      tr(STR_LUT_NEXT_IMAGE),   tr(STR_LUT_PREV_IMAGE),   tr(STR_LUT_RESET)};
  for (int i = 0; i < listCount(); ++i) {
    items[i] = fui::ListItem{};
    items[i].label = labels[i];
    items[i].actionValue = i;
  }
  fui::ListProps props{};
  props.items = items;
  props.count = listCount();
  props.action = ACTION_ROW;
  syncListViewport(screen, props);
  screen.list(props);
}

void LutLabActivity::stepWallpaper(int delta) {
  // ponytail: O(n) directory walk per explicit browse press, constant memory.
  // Use the SD index if browsing latency becomes a problem; never load a folder
  // into RAM. Scan both supported folders plus root overrides, in path order.
  BusyBanner banner(renderer, tr(STR_CHECKING_WALLPAPERS));
  auto& lab = APP_STATE.lutLab;
  std::string neighbour;
  std::string wrap;
  std::string candidate;
  // Three bounded paths on the heap avoid a folder-sized vector and keep
  // 3 * 264 bytes off the task stack; reserve once, reuse through the scan.
  neighbour.reserve(264);
  wrap.reserve(264);
  candidate.reserve(264);
  const auto consider = [&](const std::string& path) {
    const bool before = path < lab.wallpaper;
    const bool after = path > lab.wallpaper;
    if (wrap.empty() || (delta > 0 ? path < wrap : path > wrap)) wrap = path;
    if ((delta > 0 ? after : before) && (neighbour.empty() || (delta > 0 ? path < neighbour : path > neighbour)))
      neighbour = path;
  };
  for (const char* path : {"/sleep.bmp", "/sleep.pxc"}) {
    if (Storage.exists(path)) {
      candidate = path;
      consider(candidate);
    }
  }
  unsigned scanned = 0;
  for (const char* folder : {"/sleep", "/.sleep"}) {
    auto dir = Storage.open(folder);
    if (!dir || !dir.isDirectory()) continue;
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      if ((++scanned & 0x3F) == 0) {
        resetTaskWatchdogIfSubscribed();
        vTaskDelay(1);
        busy::tick();
      }
      if (file.isDirectory()) continue;
      if (!file.getName(filename, sizeof(filename))) continue;
      if (crosspoint::sleep::isWallpaperName(filename)) {
        candidate = folder;
        candidate += '/';
        candidate += filename;
        consider(candidate);
      }
    }
  }
  lab.wallpaper = neighbour.empty() ? wrap : neighbour;
  if (lab.wallpaper.empty()) lab.pinned = false;
  lab.imageFailed = false;
}

void LutLabActivity::activateIndex(int index) {
  if (!display.supportsLutLab()) return;
  // Pause the render task while changing the strings it borrows. Directory
  // browsing uses BusyBanner (main-task painting), like the existing browsers.
  {
    RenderLock lock(*this);
    auto& lab = APP_STATE.lutLab;
    switch (index) {
      case 0:
        lab.variant = lutlab::stepVariant(lab.variant, 1);
        break;
      case 1:
        lab.variant = lutlab::stepVariant(lab.variant, -1);
        break;
      case 2:
        if (lab.wallpaper.empty()) stepWallpaper(1);
        if (!lab.wallpaper.empty()) lab.pinned = !lab.pinned;
        break;
      case 3:
        stepWallpaper(1);
        break;
      case 4:
        stepWallpaper(-1);
        break;
      case 5:
        lab.variant = 0;
        lab.pinned = false;
        lab.imageFailed = false;
        break;
      default:
        return;
    }
    dirty = true;
  }
  requestUpdate();
}
