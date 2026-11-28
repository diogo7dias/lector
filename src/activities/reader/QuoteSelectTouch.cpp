// Touch selection for QuoteSelectActivity: drag across words, then Save or Look Up.
#include <Logging.h>
#include <Memory.h>

#include <climits>
#include <cstdlib>

#include "CrossPointSettings.h"
#include "DictionaryDefinitionActivity.h"
#include "QuoteSelectActivity.h"
#include "util/Dictionary.h"
#include "util/DictionaryFailure.h"

// The word nearest a touch point, rows first: a finger between two lines picks the line
// it is closer to, then the word in it. -1 when no word is within maxGap px on both axes.
int QuoteSelectActivity::nearestWord(const int x, const int y, const int maxGap) const {
  const auto gap = [](const int v, const int lo, const int len) {
    return v < lo ? lo - v : (v >= lo + len ? v - (lo + len) + 1 : 0);
  };
  int best = -1;
  long bestScore = LONG_MAX;
  for (int i = 0; i < static_cast<int>(words.size()); i++) {
    const int dx = gap(x, words[i].x, words[i].width);
    const int dy = gap(y, words[i].y, words[i].height);
    if (dx > maxGap || dy > maxGap) continue;
    const long score = static_cast<long>(dy) * 10000 + dx;
    if (score < bestScore) {
      bestScore = score;
      best = i;
    }
  }
  return best;
}

// Moves the end being dragged to `index`. Crossing the other end swaps them, so the start
// never passes the end. The start word only moves on its own page; on a page the quote has
// run into, the selection starts at the top and only the end follows the finger.
void QuoteSelectActivity::dragTo(const int index) {
  const int oldStart = startWord;
  const int oldCursor = cursor;
  if (pageNumber != startPageNumber) {
    cursor = index;
  } else if (drag == Drag::End) {
    if (index >= startWord) {
      cursor = index;
    } else {
      cursor = startWord;
      setStartWord(index);
      drag = Drag::Start;
    }
  } else if (index <= cursor) {
    setStartWord(index);
  } else {
    setStartWord(cursor);
    cursor = index;
    drag = Drag::End;
  }
  if (startWord != oldStart || cursor != oldCursor) {
    heldWord = false;  // dragged: the lift keeps the selection instead of looking a word up
    requestUpdate();
  }
}

// Slow drags are tracked pass by pass with isScreenTouchHeld. The contact is spent on the
// lift, so its release neither taps the hint bar nor reaches the reader underneath.
bool QuoteSelectActivity::handleTouch() {
  int x = 0;
  int y = 0;
  const bool held = mappedInput.isScreenTouchHeld(x, y);
  if (drag != Drag::None) {
    if (held) {
      const int hit = nearestWord(x, y, INT_MAX);
      if (hit >= 0) dragTo(hit);
      return true;
    }
    mappedInput.spendTouchContact();
    drag = Drag::None;
    if (!heldWord) {
      requestUpdate();  // the hints change to Cancel / Save / Look Up
      return true;
    }
    heldWord = false;
    // A hold lifted without a drag: the word lookup the hold has always been.
    if (SETTINGS.dictionaryName[0] == '\0') {
      ActivityResult noDictionary;
      noDictionary.isCancelled = true;  // the reader says "No dictionary set"
      setResult(std::move(noDictionary));
      finish();
    } else {
      lookUpSelection(/*fromHold=*/true);
    }
    return true;
  }

  // A new contact. One on the hint bar is a button press, answered through wasPressed.
  if (!held || words.empty() || mappedInput.isInHintBand(x, y)) return false;
  const int hit = nearestWord(x, y, 4);
  if (hit < 0) return false;
  if (phase == Phase::SelectStart) {
    cursor = hit;
    setStartWord(hit);
    phase = Phase::SelectEnd;
    drag = Drag::End;
  } else {
    const bool startHere = pageNumber == startPageNumber && std::abs(hit - startWord) < std::abs(hit - cursor);
    drag = startHere ? Drag::Start : Drag::End;
    dragTo(hit);
  }
  touchSelection = true;
  requestUpdate();
  return true;
}

// Looks the selection up as one phrase: Dictionary::lookup trims the edge punctuation the
// joined quote keeps. Leaving the definition returns to the screen the user saw open it: this
// picker after its Look Up key, the page after a held word (the picker was never shown).
void QuoteSelectActivity::lookUpSelection(const bool fromHold) {
  const std::string phrase = selectedText();
  if (phrase.empty()) return;

  // On the heap: only for this lookup, and the definition screen copies what it needs.
  auto dict = makeUniqueNoThrow<Dictionary>();
  dict_failure::Message message{StrId::STR_DICT_LOW_MEMORY, true};
  std::string definition;
  std::string headword;
  const auto showBusy = [](void* ctx, const StrId id) {
    auto* self = static_cast<QuoteSelectActivity*>(ctx);
    self->popupMsg = id;
    self->showPopup = true;
    self->requestUpdateAndWait();  // paint the page + busy popup before blocking on SD
  };
  if (!dict) {
    LOG_ERR("QUOTE", "OOM: Dictionary");
  } else if (dict_failure::lookUpWithFallbacks(*dict, SETTINGS.dictionaryName, SETTINGS.dictionaryFallback != 0,
                                               phrase.c_str(), definition, headword, message, showBusy, this)) {
    showPopup = false;
    startActivityForResult(
        std::make_unique<DictionaryDefinitionActivity>(
            renderer, mappedInput, std::move(headword), std::move(definition), dict->definitionsAreHtml(),
            /*readingSurface=*/true, /*coverPath=*/std::string{},
            DictionaryDefinitionActivity::VocabContext{phrase, epub.getTitle(), true}),
        [this, fromHold](const ActivityResult&) { fromHold ? finish() : requestUpdate(); });
    return;
  }
  popupMsg = message.id;
  showPopup = true;
  popupShownAt = millis();
  requestUpdate();
}
