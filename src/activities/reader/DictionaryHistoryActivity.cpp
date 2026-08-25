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

namespace fui = freeink::ui;

namespace {
constexpr unsigned long POPUP_DURATION_MS = 1500;

// Same yield the page-side lookup passes: an index build runs long enough to starve the
// watchdog without it.
void indexBuildYield(void*) { vTaskDelay(1); }
}  // namespace

void DictionaryHistoryActivity::onEnter() {
  DICT_HISTORY.ensureLoaded();
  UiListActivity::onEnter();
}

void DictionaryHistoryActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
}

const char* DictionaryHistoryActivity::headerTitle() const { return tr(STR_LOOKUP_HISTORY); }

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

void DictionaryHistoryActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (isClearRow(static_cast<size_t>(index))) {
    confirmClear();
    return;
  }
  const auto& words = DICT_HISTORY.getWords();
  if (static_cast<size_t>(index) >= words.size()) return;
  // By value: the lookup pushes an activity, and the store can be written under it.
  const std::string word = words[index];
  lookUp(word);
}

void DictionaryHistoryActivity::confirmClear() {
  if (DICT_HISTORY.empty()) return;
  startActivityForResult(std::make_unique<ConfirmationActivity>(
                             renderer, mappedInput, std::string(tr(STR_CLEAR_LOOKUP_HISTORY)), std::string()),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             DICT_HISTORY.clear();
                             nav.reset();
                           }
                           requestUpdate();
                         });
}

bool DictionaryHistoryActivity::handleCustomInput() {
  if (popup == Popup::None) return false;
  if (popup == Popup::NotFound || popup == Popup::Error) {
    if (millis() - popupShownAt >= POPUP_DURATION_MS) {
      popup = Popup::None;
      requestUpdate();
    }
  }
  return true;
}

void DictionaryHistoryActivity::onBackButton() {
  ActivityResult res;
  res.isCancelled = true;
  setResult(std::move(res));
  finish();
}

void DictionaryHistoryActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing), 0,
                  static_cast<int16_t>(metrics.buttonHintsHeight + metrics.verticalSpacing), 0});

  const auto& words = DICT_HISTORY.getWords();
  if (words.empty()) {
    screen.centeredText(tr(STR_NO_LOOKUPS));
    return;
  }

  rows.assign(static_cast<size_t>(rowCount()), fui::ListItem{});
  for (size_t i = 0; i < words.size(); ++i) {
    rows[i].label = words[i].c_str();
    rows[i].actionValue = static_cast<int16_t>(i);
  }
  // Last row clears the history; it only exists while there is history to clear.
  rows.back().label = tr(STR_CLEAR_HISTORY);
  rows.back().actionValue = static_cast<int16_t>(words.size());

  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  syncListViewport(screen, props);
  screen.list(props);
}

bool DictionaryHistoryActivity::drawOverlay() {
  if (popup == Popup::None) return false;
  // drawPopup overlays the framebuffer and refreshes the display itself.
  // I18N.get directly: tr() only accepts literal key names.
  GUI.drawPopup(renderer, I18N.get(popupMsg));
  return true;
}
