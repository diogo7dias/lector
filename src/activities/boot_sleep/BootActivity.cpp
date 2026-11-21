#include "BootActivity.h"

#include <GfxRenderer.h>

#include "LectorFace.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  renderer.clearScreen();
  // The same face the sleep screen falls back to.
  drawLectorFace(renderer);
  // HALF so a FAST pass never leaves the retained frame under the face.
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
