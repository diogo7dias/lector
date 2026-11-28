// Grab Quote's save path: append the selected passage to the book's quotes sidecar.
#include <Arduino.h>
#include <GrowthBounds.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include "QuoteSelectActivity.h"
#include "QuoteSidecar.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "util/QuoteSync.h"
#include "util/QuoteText.h"

std::string QuoteSelectActivity::chapterTitle() const {
  const int tocIndex = epub.getTocIndexForSpineIndex(spineIndex);
  if (tocIndex >= 0) return epub.getTocItem(tocIndex).title;
  return std::string(tr(STR_CHAPTER)) + " " + std::to_string(spineIndex + 1);
}

void QuoteSelectActivity::openNoteEntry() {
  if (startWord < 0) return;
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, std::string(tr(STR_ADD_NOTE)), std::string(),
                                              quote_text::MAX_NOTE_LENGTH, InputType::Text),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          if (const auto* kr = std::get_if<KeyboardResult>(&result.data)) {
            saveSelectedQuote(kr->text);
            finish();
            return;
          }
        }
        requestUpdate();
      });
}

void QuoteSelectActivity::saveSelectedQuote(const std::string& note) {
  if (startWord < 0) return;
  const std::string quote = selectedText();
  if (quote.empty()) return;

  // Where this passage lives, so the reader can underline it on later visits. The
  // anchor was taken on the start word's own page (see the Confirm handler): the word
  // index is only a tie-break for a passage repeated inside the same paragraph, and
  // the quote text itself is what identifies the words.
  quote_text::QuoteMeta meta;
  meta.savedAt = quote_sync::nowUtc();
  // The start page's place in the chapter, in KOReader's units: what BookOrbit is told.
  if (section) {
    if (const auto offset = section->getVisibleTextOffsetForPage(static_cast<uint16_t>(startPageNumber))) {
      meta.textOffset = *offset;
    }
  }
  const std::string cleanNote = note.empty() ? std::string() : quote_text::sanitizeNote(note);
  if (!saveQuoteToFile(quote, quote_text::formatAnchorToken(startAnchor), meta, cleanNote)) {
    // Full sidecar or a failed write: say so before the reader repaints over it.
    GUI.drawPopup(renderer, tr(STR_QUOTE_SAVE_FAILED));
    delay(1200);
    return;
  }
  appendClipping(quote, cleanNote, meta.savedAt);
}

// The Kindle copy of the quote (and optional Note entry). The book's own quotes file is
// the record: a failure here is only logged, and the save still counts.
void QuoteSelectActivity::appendClipping(const std::string& quote, const std::string& note,
                                         const uint32_t savedAt) const {
  const int64_t localSec = savedAt ? quote_sync::toLocal(savedAt) : 0;
  const std::string chapter = chapterTitle();
  std::string entry =
      quote_text::formatClipping(epub.getTitle(), epub.getAuthor(), chapter, startPageNumber + 1, quote, localSec);
  if (!note.empty()) {
    entry +=
        quote_text::formatNoteClipping(epub.getTitle(), epub.getAuthor(), chapter, startPageNumber + 1, note, localSec);
  }
  HalFile file = Storage.open(quote_text::CLIPPINGS_PATH, O_RDWR | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("QUOTE", "Cannot open %s", quote_text::CLIPPINGS_PATH);
    return;
  }
  if (file.write(entry.data(), entry.size()) != entry.size()) {
    LOG_ERR("QUOTE", "Clipping write failed");
    return;
  }
  LOG_INF("QUOTE", "Clipping added to %s", quote_text::CLIPPINGS_PATH);
}

// Atomic-ish read-modify-write append of one "[chapter @anchor]\nquote\n(> note\n)---\n\n"
// entry to the "<book>_QUOTES.txt" sidecar: stream existing bytes + the new entry
// into a .tmp, then rotate primary -> .bak, .tmp -> primary, drop .bak (restore
// .bak on promote failure). Refused when the file would exceed MAX_QUOTES_FILE_BYTES.
bool QuoteSelectActivity::saveQuoteToFile(const std::string& quote, const std::string& anchorToken,
                                          const quote_text::QuoteMeta& meta, const std::string& note) {
  const std::string path = quote_text::quotesFilePathFor(epub.getPath());
  const std::string tmpPath = path + ".tmp";
  const std::string entry = quote_text::formatQuoteEntry(chapterTitle(), anchorToken, quote, meta, note);

  size_t existingSize = 0;
  if (Storage.exists(path.c_str())) {
    // Unreadable is not empty: the rewrite below would replace every saved quote with this one.
    HalFile probe;
    if (!Storage.openFileForRead("QUOTE", path, probe)) {
      LOG_ERR("QUOTE", "Cannot read existing quotes file, not saving");
      return false;
    }
    existingSize = probe.size();
  }
  if (!memory::canGrowWithinLimit(existingSize, entry.size(), quote_text::MAX_QUOTES_FILE_BYTES)) {
    LOG_ERR("QUOTE", "Quotes file full: %u + %u > %u", static_cast<unsigned>(existingSize),
            static_cast<unsigned>(entry.size()), static_cast<unsigned>(quote_text::MAX_QUOTES_FILE_BYTES));
    return false;
  }

  // Build the new file content in the temp file.
  {
    HalFile out;
    if (!Storage.openFileForWrite("QUOTE", tmpPath, out)) {
      LOG_ERR("QUOTE", "Cannot open temp quotes file for write");
      return false;
    }
    if (existingSize > 0) {
      HalFile in;
      if (!Storage.openFileForRead("QUOTE", path, in)) {
        LOG_ERR("QUOTE", "Cannot reopen existing quotes file for copy");
        out.close();
        Storage.remove(tmpPath.c_str());
        return false;
      }
      uint8_t buffer[512];
      size_t copied = 0;
      int n;
      while ((n = in.read(buffer, sizeof(buffer))) > 0) {
        if (out.write(buffer, static_cast<size_t>(n)) != static_cast<size_t>(n)) {
          LOG_ERR("QUOTE", "Temp quotes write failed (copy)");
          in.close();
          out.close();
          Storage.remove(tmpPath.c_str());
          return false;
        }
        copied += static_cast<size_t>(n);
      }
      in.close();
      if (copied != existingSize) {
        LOG_ERR("QUOTE", "Short read copying quotes: %u of %u", static_cast<unsigned>(copied),
                static_cast<unsigned>(existingSize));
        out.close();
        Storage.remove(tmpPath.c_str());
        return false;
      }
    }
    if (out.write(entry.data(), entry.size()) != entry.size()) {
      LOG_ERR("QUOTE", "Temp quotes write failed (entry)");
      out.close();
      Storage.remove(tmpPath.c_str());
      return false;
    }
    out.flush();
    out.close();
  }

  // Promote temp -> primary. Keep a backup so a failed promote can be undone.
  if (!quote_sidecar::promoteTmp(path, "QUOTE")) return false;
  LOG_INF("QUOTE", "Saved quote (%u bytes) to %s", static_cast<unsigned>(entry.size()), path.c_str());
  return true;
}
