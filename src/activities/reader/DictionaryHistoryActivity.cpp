#include "DictionaryHistoryActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "CrossPointSettings.h"
#include "DictHistoryStore.h"
#include "DictionaryDefinitionActivity.h"
#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/DictionaryFailure.h"

namespace {
constexpr unsigned long POPUP_DURATION_MS = 1500;

// Same yield the page-side lookup passes: an index build runs long enough to starve the
// watchdog without it.
void indexBuildYield(void*) { vTaskDelay(1); }
}  // namespace

void DictionaryHistoryActivity::onEnter() {
  Activity::onEnter();
  DICT_HISTORY.ensureLoaded();
  selectorIndex = 0;
  requestUpdate();
}

size_t DictionaryHistoryActivity::wordCount() const { return DICT_HISTORY.getWords().size(); }

int DictionaryHistoryActivity::rowCount() const {
  const int words = static_cast<int>(wordCount());
  return words > 0 ? words + 1 : 0;  // the clear row only exists once there is history
}

bool DictionaryHistoryActivity::isClearRow(const size_t index) const { return wordCount() > 0 && index == wordCount(); }

void DictionaryHistoryActivity::lookUp(const std::string& word) {
  popup = Popup::Busy;
  if (!dictOpenAttempted) {
    dictOpenAttempted = true;
    dictOpenOk = dict.open(SETTINGS.dictionaryName);
    // needsIndex() opens and validates the .qidx sidecar, so ask it once per open rather
    // than once per word: the answer only changes when the sidecar is built below.
    dictNeedsIndex = dictOpenOk && dict.needsIndex();
  }
  popupMsg = dictNeedsIndex ? StrId::STR_DICT_INDEXING : StrId::STR_DICT_LOOKING_UP;
  requestUpdateAndWait();  // paint the list + busy popup before blocking on SD

  bool ok = dictOpenOk;
  Dictionary::IndexResult indexResult = Dictionary::IndexResult::Ok;
  if (ok && dictNeedsIndex) {
    ok = dict.buildIndex(&indexBuildYield, nullptr, &indexResult);
    dictNeedsIndex = !ok;  // a successful build leaves the sidecar fresh; a failed one retries
    if (ok) {
      // The build is over; the wait that remains is the lookup, so say so.
      popupMsg = StrId::STR_DICT_LOOKING_UP;
      requestUpdateAndWait();
    }
  }

  std::string definition;
  std::string headword;
  Dictionary::LookupResult result = Dictionary::LookupResult::NotFound;
  if (ok && dict.lookup(word.c_str(), definition, headword, &result)) {
    popup = Popup::None;
    startActivityForResult(
        std::make_unique<DictionaryDefinitionActivity>(renderer, mappedInput, std::move(headword),
                                                       std::move(definition), dict.definitionsAreHtml()),
        [this](const ActivityResult&) { requestUpdate(); });
    return;
  }

  // A word in the history was in the dictionary when it was looked up, so a miss here
  // usually means the dictionary itself changed — but a read or memory failure must not be
  // reported as a miss, which is why the wording is shared with the page-side lookup.
  const dict_failure::Message message = ok ? dict_failure::forLookup(result) : dict_failure::forIndex(indexResult);
  popup = message.isError ? Popup::Error : Popup::NotFound;
  popupMsg = message.id;
  popupShownAt = millis();
  requestUpdate();
}

void DictionaryHistoryActivity::activateSelected() {
  if (isClearRow(selectorIndex)) {
    confirmClear();
    return;
  }
  const auto& words = DICT_HISTORY.getWords();
  if (selectorIndex >= words.size()) return;
  // By value: the lookup pushes an activity, and the store can be written under it.
  const std::string word = words[selectorIndex];
  lookUp(word);
}

void DictionaryHistoryActivity::confirmClear() {
  if (DICT_HISTORY.empty()) return;
  startActivityForResult(std::make_unique<ConfirmationActivity>(
                             renderer, mappedInput, std::string(tr(STR_CLEAR_LOOKUP_HISTORY)), std::string()),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             DICT_HISTORY.clear();
                             selectorIndex = 0;
                           }
                           requestUpdate();
                         });
}

void DictionaryHistoryActivity::loop() {
  if (popup == Popup::NotFound || popup == Popup::Error) {
    if (millis() - popupShownAt >= POPUP_DURATION_MS) {
      popup = Popup::None;
      requestUpdate();
    }
    return;
  }

  // A tap on a row selects and activates it, the same as every other list.
  int tappedRow = 0;
  if (mappedInput.wasRowTapped(tappedRow) && tappedRow >= 0 && tappedRow < rowCount()) {
    selectorIndex = static_cast<size_t>(tappedRow);
    activateSelected();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
    return;
  }

  const int listSize = rowCount();
  if (listSize <= 0) return;
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, true);

  buttonNavigator.onNextPress([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });
  buttonNavigator.onPreviousPress([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
}

void DictionaryHistoryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_LOOKUP_HISTORY));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  const auto& words = DICT_HISTORY.getWords();
  if (words.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_LOOKUPS));
  } else {
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, rowCount(), static_cast<int>(selectorIndex),
                 [this, &words](int index) {
                   if (isClearRow(static_cast<size_t>(index))) return std::string(tr(STR_CLEAR_HISTORY));
                   return words[index];
                 });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (popup != Popup::None) {
    // drawPopup overlays the framebuffer and refreshes the display itself.
    // I18N.get directly: tr() only accepts literal key names.
    GUI.drawPopup(renderer, I18N.get(popupMsg));
    return;
  }

  renderer.displayBuffer();
}
