#include "CleanStorageActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"

void CleanStorageActivity::onEnter() {
  Activity::onEnter();

  state = WARNING;
  const char* options[] = {tr(STR_CANCEL), tr(STR_CLEAN_BUTTON)};
  confirmPopup.show(tr(STR_CLEAN_STORAGE), options, 2, 0, [this](int idx) {
    if (idx == 1) {
      beginClean();
    } else {
      goBack();
    }
  });
  requestUpdate();
}

void CleanStorageActivity::onExit() { Activity::onExit(); }

void CleanStorageActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CLEAN_STORAGE));

  if (state == WARNING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 40, tr(STR_CLEAN_STORAGE_WARNING_1), true,
                              EpdFontFamily::REGULAR);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, tr(STR_CLEAN_STORAGE_WARNING_2), true,
                              EpdFontFamily::REGULAR);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 30, tr(STR_CLEAN_STORAGE_WARNING_3), true);

    if (confirmPopup.processRender(renderer, mappedInput)) return;

    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_CLEAN_BUTTON), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == CLEANING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_CLEANING_STORAGE));
    renderer.displayBuffer();
    return;
  }

  if (state == SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_STORAGE_CLEANED), true,
                              EpdFontFamily::REGULAR);
    std::string resultText = std::to_string(removedCount) + " " + std::string(tr(STR_ITEMS_REMOVED)) + ", " +
                             std::to_string(keptCount) + " " + std::string(tr(STR_ITEMS_KEPT));
    if (failedCount > 0) {
      resultText += ", " + std::to_string(failedCount) + " " + std::string(tr(STR_FAILED_LOWER));
    }
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, resultText.c_str());

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_CLEAN_STORAGE_FAILED), true,
                              EpdFontFamily::REGULAR);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, tr(STR_CHECK_SERIAL_OUTPUT));

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
}

void CleanStorageActivity::beginClean() {
  LOG_DBG("CLEAN_STORAGE", "User confirmed, starting orphan sweep");
  {
    RenderLock lock(*this);
    state = CLEANING;
  }
  // The sweep blocks this task for as long as it runs, so the "cleaning" frame has
  // to reach the panel before it starts or the user stares at the warning screen.
  requestUpdateAndWait();
  cleanStorage();
}

void CleanStorageActivity::cleanStorage() {
  // cleanOrphanBookCaches deletes nothing at all unless it could enumerate every
  // book on the card first, because a book it failed to see is indistinguishable
  // from an orphan and its cache holds that book's reading progress.
  if (!cleanOrphanBookCaches(removedCount, keptCount, failedCount)) {
    LOG_ERR("CLEAN_STORAGE", "sweep aborted, nothing removed");
    state = FAILED;
    requestUpdate();
    return;
  }

  state = SUCCESS;
  requestUpdate();
}

void CleanStorageActivity::loop() {
  if (state == WARNING) {
    if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      beginClean();
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      LOG_DBG("CLEAN_STORAGE", "User cancelled");
      goBack();
    }
    return;
  }

  if (state == SUCCESS || state == FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }
}
