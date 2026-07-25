#include "SdSleepImageFs.h"

#include <HalStorage.h>

#include <cstdint>
#include <cstring>

#include "WallpaperNames.h"
#include "util/BusyTick.h"
#include "util/TaskWatchdog.h"

namespace crosspoint {
namespace sleep {

namespace {

// A wallpaper folder can hold thousands of files, and the SD walk blocks the
// task the whole time.
constexpr uint32_t WDT_YIELD_INTERVAL = 128;

}  // namespace

void SdSleepImageFs::walk(const char* dir, const NameSink& sink) {
  auto handle = Storage.open(dir);
  if (!handle || !handle.isDirectory()) {
    if (handle) handle.close();
    return;
  }

  uint32_t seen = 0;
  char name[256];  // FAT long-file-name maximum (255 characters plus terminator)
  for (auto entry = handle.openNextFile(); entry; entry = handle.openNextFile()) {
    if (!entry.isDirectory()) {
      entry.getName(name, sizeof(name));
      // The name points at this stack buffer and is only valid for the duration
      // of the call — the sink copies what it retains.
      if (isWallpaperName(name)) sink(name, strlen(name));
    }
    entry.close();
    if (++seen % WDT_YIELD_INTERVAL == 0) {
      resetTaskWatchdogIfSubscribed();
      yield();
      // Stepping through a big wallpaper folder scans it once per step, so the
      // UI needs a way in to say what is taking the time.
      busy::tick();
    }
  }
  handle.close();
}

bool SdSleepImageFs::mkdir(const char* path) { return Storage.mkdir(path); }

bool SdSleepImageFs::rename(const char* from, const char* to) { return Storage.rename(from, to); }

}  // namespace sleep
}  // namespace crosspoint
