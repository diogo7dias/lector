#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderActivity.h"
#include "QuoteSelectActivity.h"
#include "ReaderPresetStore.h"
#include "ReaderUtils.h"
#include "SdCardFontSystem.h"
#include "activities/network/NearbyFileTransferActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "util/BookProgressFile.h"
#include "util/SortesSelection.h"

std::string EpubReaderActivity::readerOverridePath() const { return readerSidecarPath(epub->getCachePath()); }

void EpubReaderActivity::loadReaderPrefs() {
  const BookReaderPrefs book = loadBookReaderPrefs(epub->getCachePath(), ReaderPrefs::fromGlobal());
  prefs_ = book.prefs;
  prefsCustom_ = book.custom;
  prefsFromVersion_ = book.fromVersion;
  // An upgraded sidecar is not applied here. The saved page number was produced by
  // the OLD layout, so the chapter is laid out that way first, the reading position
  // is read off it as a paragraph, and only then do the new defaults go in — see
  // applyPendingPrefsMigration(). Applying them now would land the reader on a page
  // number that no longer means anything.
  pendingPrefsMigration_ = book.migrated;
}

bool EpubReaderActivity::writeReaderOverride(const ReaderPrefs& p) const {
  if (sortesMode) return true;
  HalFile f;
  if (!Storage.openFileForWrite("ERS", readerOverridePath(), f)) {
    LOG_ERR("ERS", "Failed to open reader_override.bin for write");
    return false;
  }
  if (!writeReaderPrefs(f, p)) {
    LOG_ERR("ERS", "Short write to reader_override.bin");
    return false;
  }
  return true;
}

void EpubReaderActivity::reloadForReaderPrefsChange() {
  // Mirrors applyOrientation's reflow: keep the reading position, drop the section
  // so the next render rebuilds it with the new ReaderPrefs (CrossPoint's section
  // cache re-keys on the changed spec automatically — no cache machinery of ours).
  RenderLock lock(*this);
  dropSectionForRelayout();
  // The loader skips a book's stylesheet when its look had both embedded switches off,
  // so a book that has just turned one on may have no CSS cache to lay out with.
  // No banner: a setting change goes straight back to the page, without a wait screen.
  if (wantsBookCss(prefs_) && !epub->hasCssCache()) epub->ensureCssCache();
}

void EpubReaderActivity::dropSectionForRelayout() {
  // Caller must already hold the render lock. RenderLock wraps xSemaphoreCreateMutex,
  // which is NOT recursive, so the render path takes this entry point instead of
  // reloadForReaderPrefsChange() — taking the mutex twice on one task blocks forever.
  if (section) {
    // The content offset is what makes the rebuild stop in the right place; the
    // paragraph below is what picks the landing page once it has.
    cacheSectionPosition();
    // nextPageNumber is kept only as the fallback: the new layout may have a different
    // page count entirely, so the paragraph below is what actually restores the place.
    captureOrdinalAnchor();
  }
  section.reset();
  // A settings change goes straight back to the page: no Indexing popup on the rebuild.
  quietRelayout_ = true;
  // Every glyph on the page is about to move, so the next paint cannot be a differential
  // one: a FAST refresh only drives the pixels it thinks changed and leaves the old font,
  // size or spacing ghosted underneath the new layout. Put the next page on the cleanup
  // path, which drives every pixel to its target.
  scheduleGhostCleanup();
  // Quote underlines are memoised per (spine, page). A relayout keeps both of those
  // numbers but moves every word, so the remembered segments must go with the section.
  underlineMemoSpine = -1;
  underlineMemoPage = -1;
  underlineMemo.clear();
}

void EpubReaderActivity::readerEditSinkThunk(void* ctx, const ReaderPrefs& live) {
  auto* self = static_cast<EpubReaderActivity*>(ctx);
  self->readerEdit_ = live;
  self->persistReaderSettingsEdit(live);
}

void EpubReaderActivity::persistReaderSettingsEdit(const ReaderPrefs& live) const {
  if (sortesMode) return;
  const ReaderOverrideDecision decision = decideReaderOverride(live, prefs_, prefsCustom_);
  switch (decision.action) {
    case ReaderOverrideAction::Write:
      writeReaderOverride(decision.prefs);
      break;
    case ReaderOverrideAction::Remove:
      Storage.remove(readerOverridePath().c_str());
      break;
    case ReaderOverrideAction::Keep:
      break;
  }
}

void EpubReaderActivity::applyReaderSettingsEdit() {
  ReaderPrefs edited = readerEdit_;
  // The in-book toggles are not part of the Reader Settings screen, so they are not in
  // the screen's round-trip; carry the book's values across so editing font or margins
  // never resets them.
  restoreBookOnlyFields(edited, prefs_);
  if (std::memcmp(&edited, &prefs_, sizeof(ReaderPrefs)) == 0) {
    // Opened the settings screen but changed nothing — leave the book as it was.
    requestUpdate();
    return;
  }
  prefs_ = edited;
  prefsCustom_ = true;
  writeReaderOverride(prefs_);
  reloadForReaderPrefsChange();
  requestUpdate();
}

void EpubReaderActivity::statusBarEditSinkThunk(void* ctx, const StatusBarBlock& edited) {
  static_cast<EpubReaderActivity*>(ctx)->persistStatusBarEdit(edited);
}

void EpubReaderActivity::persistStatusBarEdit(const StatusBarBlock& edited) {
  statusBarEdit_ = edited;
  if (sortesMode) return;
  // Every change lands on the card straight away, so switching the reader off inside
  // the screen keeps it. The bar is the reader's own preference, not this book's look:
  // it becomes the global bar, so the next book opens with it. A book following global
  // keeps following it and needs no sidecar; a book with its own look takes the bar too.
  //
  // Except the show/hide switch and the hidden-bar progress bar: those are this book's own,
  // like the quick toggle that sets them (applyStatusBar). Copied to global, a bar hidden
  // in one book would vanish from every book the moment any other row was touched here.
  setGlobalBarLayout(edited);
  // Changing either own field gives the book its sidecar, as the quick toggle does.
  if (edited.enabled != prefs_.statusBarEnabled || edited.offBar != prefs_.sbOffBar) prefsCustom_ = true;
  if (!prefsCustom_) return;
  // Written on every change, not only when it differs from the book on open: a bar
  // changed and then put back must not leave the middle value on the card.
  ReaderPrefs p = prefs_;
  setStatusBarOf(p, edited);
  writeReaderOverride(p);
}

void EpubReaderActivity::setGlobalBarLayout(const StatusBarBlock& bar) {
  // Every book opens with the global layout (settleSidecarPrefs), so a bar chosen in one
  // book has to land there. The show/hide switch and hidden-bar progress bar stay global's.
  const StatusBarBlock global = SETTINGS.statusBar();
  StatusBarBlock toGlobal = bar;
  toGlobal.enabled = global.enabled;
  toGlobal.offBar = global.offBar;
  if (std::memcmp(&global, &toGlobal, sizeof(StatusBarBlock)) == 0) return;
  SETTINGS.setStatusBar(toGlobal);
  SETTINGS.saveToFile();
}

void EpubReaderActivity::applyStatusBarEdit() {
  ReaderPrefs edited = prefs_;
  setStatusBarOf(edited, statusBarEdit_);
  if (std::memcmp(&edited, &prefs_, sizeof(ReaderPrefs)) == 0) {
    requestUpdate();
    return;
  }
  // Already on the card: persistStatusBarEdit() wrote it with the change that made it.
  // prefsCustom_ is as persistStatusBarEdit() left it: the edit went to the global bar,
  // which a book following global already reads, unless the book's own switch changed.
  prefs_ = edited;
  // The bar's height and edges decide the reserved bands, so the viewport changed and
  // the chapter has to be laid out again — the same path any margin change takes.
  reloadForReaderPrefsChange();
  requestUpdate();
}

void EpubReaderActivity::resetReaderPrefsToGlobal() {
  if (!sortesMode) Storage.remove(readerOverridePath().c_str());
  // SETTINGS only ever holds the global look (Text Settings edits a copy), so
  // fromGlobal() is the true global snapshot.
  prefs_ = ReaderPrefs::fromGlobal();
  prefsCustom_ = false;
  // An SD family keeps exactly one size resident and the id resolver returns whichever that
  // is, whatever size is asked for, so the global size has to be made resident before the
  // relayout below -- otherwise the page keeps rendering at the size the override asked for.
  sdFontSystem.ensureLoadedFor(renderer, prefs_.sdFontFamilyName, prefs_.fontPointSize);
  reloadForReaderPrefsChange();
  requestUpdate();
}

void EpubReaderActivity::applyParagraphNumbering(const uint8_t mode, const uint8_t size) {
  if (mode == prefs_.paragraphNumbering && size == prefs_.paragraphNumberSize) return;
  prefs_.paragraphNumbering = mode;
  prefs_.paragraphNumberSize = size;
  prefsCustom_ = true;
  writeReaderOverride(prefs_);
  // No re-layout: the ordinals are already baked into the page cache; only whether
  // and how they are drawn changes, so a plain repaint suffices.
  requestUpdate();
}

void EpubReaderActivity::applyStatusBar(const uint8_t enabled, const uint8_t progressBar) {
  const bool barChanged = enabled != prefs_.statusBarEnabled;
  // Both are per-book now, so both are compared against this book's prefs. The
  // value-change guard is what keeps this off the card write path on every menu close.
  const bool progressBarChanged = progressBar != prefs_.sbOffBar;
  if (!barChanged && !progressBarChanged) return;
  prefs_.statusBarEnabled = enabled;
  prefs_.sbOffBar = progressBar;
  prefsCustom_ = true;
  writeReaderOverride(prefs_);
  // Unlike the toggles above these change the reserved top/bottom bands, so the viewport
  // changes and the chapter has to be laid out again. Same path as any margin change: the
  // reading position is held as a paragraph across the rebuild. Both are applied before
  // this single reload, so changing them together costs one repagination, not two.
  reloadForReaderPrefsChange();
}

void EpubReaderActivity::applyPaperbackLook(const uint8_t body, const uint8_t status) {
  if (body == prefs_.paperbackLookBody && status == prefs_.paperbackLookStatus) return;
  prefs_.paperbackLookBody = body;
  prefs_.paperbackLookStatus = status;
  prefsCustom_ = true;
  writeReaderOverride(prefs_);
  // Paperback only changes ink weight, not layout — no re-index, just repaint.
  requestUpdate();
}

// Copies another book's saved reader settings onto this one, once. Reads that book's
// reader_override.bin directly rather than going through its Epub, since the source
// book is not open. Writing our own override marks this book custom, so a later Reset
// still returns it to the global settings.
void EpubReaderActivity::applyStolenLook(const std::string& sourceCachePath) {
  // Through the loader, so an old sidecar is settled (migrated defaults filled in) the
  // way it would be when that book opens.
  const BookReaderPrefs stolen = loadBookReaderPrefs(sourceCachePath, ReaderPrefs::fromGlobal());
  if (!stolen.custom) {
    LOG_ERR("ERS", "Steal Look: source reader_override.bin missing or unreadable");
    requestUpdate();
    return;
  }
  applyReaderPrefsFrom(stolen.prefs);
}

// Adopt a whole set of reader settings from somewhere else — another book (Steal Look)
// or a saved preset. Shared so the two cannot drift apart in how they write, reload or
// resolve the font.
//
// Returns the prefs actually adopted, which differ from the argument when the SD font
// it names is no longer installed. The caller uses that to write the correction back to
// a stored preset, so a dead font name is repaired rather than re-applied forever.
ReaderPrefs EpubReaderActivity::applyReaderPrefsFrom(const ReaderPrefs& incoming) {
  ReaderPrefs next = incoming;

  // A preset saved months ago can name an SD font that has since left the card.
  // resolveFontId answers with whatever family is currently RESIDENT rather than the
  // one asked for, so an unchecked name lays the book out at the wrong font and size on
  // every open. Fall back to the built-in family instead, and report it.
  if (next.sdFontFamilyName[0] != '\0') {
    const auto& families = sdFontSystem.registry().getFamilies();
    const bool installed = std::any_of(families.begin(), families.end(), [&](const SdCardFontFamilyInfo& fam) {
      return fam.name == next.sdFontFamilyName;
    });
    if (!installed) {
      LOG_ERR("ERS", "Reader prefs name a missing SD font '%s' — falling back to the built-in family",
              next.sdFontFamilyName);
      std::memset(next.sdFontFamilyName, 0, sizeof(next.sdFontFamilyName));
      next.fontFamily = CrossPointSettings::CHAREINK;
    }
  }

  if (std::memcmp(&next, &prefs_, sizeof(ReaderPrefs)) == 0) {
    requestUpdate();  // already identical — nothing to copy, and no re-layout to pay for
    return next;
  }
  prefs_ = next;
  prefsCustom_ = true;
  writeReaderOverride(prefs_);
  // Orientation is deliberately NOT part of ReaderPrefs here: rotating is a device
  // choice, not a book's look, so adopting a look never spins the screen.
  // The font LOAD is the authority, not resolveFontId: an SD family keeps exactly one
  // size resident, so the adopted size must be made resident before laying out with it.
  sdFontSystem.ensureLoadedFor(renderer, prefs_.sdFontFamilyName, prefs_.fontPointSize);
  reloadForReaderPrefsChange();
  requestUpdate();
  return next;
}
