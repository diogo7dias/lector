#include "QuickResumeFrame.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <Logging.h>

#include <string>

namespace quick_resume {

bool saveFrame(const GfxRenderer& renderer) {
  const size_t size = display.getBufferSize();
  const std::string part = FsHelpers::partialPathFor(FRAME_PATH);
  HalFile file;
  if (!Storage.openFileForWrite("SLP", part, file)) return false;
  const bool whole = file.write(renderer.getFrameBuffer(), size) == size;
  file.close();
  // FAT rename never overwrites: the last wake normally consumed the old frame, but a lock
  // after a boot that never woke through here can find one still there.
  const bool done = whole && (Storage.remove(FRAME_PATH), Storage.rename(part.c_str(), FRAME_PATH));
  if (!done) {
    LOG_ERR("SLP", "Quick Resume frame not saved");
    Storage.remove(part.c_str());
  }
  return done;
}

bool restoreFrame() {
  // Plain open, no exists() probe: the open answers the same question.
  HalFile file = Storage.open(FRAME_PATH, O_RDONLY);
  if (!file) return false;
  const size_t size = display.getBufferSize();
  const bool whole = file.read(display.getFrameBuffer(), size) == static_cast<int>(size);
  file.close();
  Storage.remove(FRAME_PATH);
  LOG_INF("SLP", "Quick Resume frame %s", whole ? "restored" : "short, waking as a painted face");
  return whole;
}

}  // namespace quick_resume
