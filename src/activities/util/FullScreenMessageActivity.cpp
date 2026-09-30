#include "FullScreenMessageActivity.h"

#include <GfxRenderer.h>

#include "components/ListChrome.h"

void FullScreenMessageActivity::onEnter() {
  Activity::onEnter();
  renderer.clearScreen();
  drawCentredTitlePage(renderer, text.c_str());
  renderer.displayBuffer(refreshMode);
}
