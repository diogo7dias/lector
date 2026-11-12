#include "ReleaseNotesActivity.h"

#include <I18n.h>
#include <Logging.h>

#include "CrossPointState.h"

void ReleaseNotesActivity::onEnter() {
  UiStatusActivity::onEnter();
  // Marked seen on entry, in memory first, so a failed save still shows it only once this boot.
  if (APP_STATE.releaseNotesSeen < REVISION) {
    APP_STATE.releaseNotesSeen = REVISION;
    if (!APP_STATE.saveToFile()) LOG_ERR("NOTES", "Failed to save What's New as seen");
  }
}

UiStatusActivity::StatusView ReleaseNotesActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_WHATS_NEW);
  view.headerRight = VERSION;
  view.sections[0].heading = tr(STR_NOTES_TEXT_TITLE);
  view.sections[0].paragraph = tr(STR_NOTES_TEXT_BODY);
  view.sections[1].heading = tr(STR_NOTES_BOOKS_TITLE);
  view.sections[1].paragraph = tr(STR_NOTES_BOOKS_BODY);
  view.sections[2].heading = tr(STR_NOTES_FILES_TITLE);
  view.sections[2].paragraph = tr(STR_NOTES_FILES_BODY);
  // Room for a translation that runs longer than the English; the body has space for all three.
  for (auto& section : view.sections) section.paragraphMaxLines = 6;
  view.confirmHint = tr(STR_OK_BUTTON);
  view.acceptLabel = tr(STR_OK_BUTTON);
  return view;
}
