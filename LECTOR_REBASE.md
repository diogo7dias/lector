# Lector Re-base onto CrossPoint — Project Log

> Living document. Updated every step. This is the source of truth for the
> lector re-base so work can resume after any context compaction.
> Last updated: 2026-07-24.

## Goal

Stop maintaining lector as a giant diverged fork. Re-base lector onto **upstream
CrossPoint + freeink-sdk** as a thin layer of "niceties", and **track upstream
forever** (merge new CrossPoint releases to ride their improvements for free).

Bonus already gained: moving to the CrossPoint base auto-removes the three
fork-only slow/stuck regressions that plagued the old fork (they were our code),
and restores upstream features we had dropped.

## Base

- **Upstream:** `crosspoint-reader/crosspoint-reader` (remote `upstream`).
- **Base version:** CrossPoint **1.5.0** — git tag `1.5.0` == `develop` HEAD `66abde5` (cut 2026-07-23).
- **SDK:** `Free-Ink/freeink-sdk@main` submodule (renamed `FreeInkDisplay`, per-controller drivers). Pulls a nested Lucide icons submodule.
- One firmware serves **both X3 and X4**.

## Mechanics

- Repo: `diogo7dias/lector`. Added remote `upstream`.
- Work branch: **`crosspoint-rebase`** (off `upstream/develop`), pushed to origin.
- Worktree: `.claude/worktrees/crosspoint-base` (keeps `main` = old lector v0.75.0 flashable as a fallback).
- Build: `cd .claude/worktrees/crosspoint-base && pio run` (default env, ~1 min after first). First checkout needs `git submodule update --init --recursive`.
- Host tests: `test/` (CMake+gtest), same as before.
- Track upstream going forward: `git fetch upstream && git merge upstream/develop` (or the next tag).
- End state: `crosspoint-rebase` eventually replaces `main`.

## HARD CONSTRAINTS (do not violate)

1. **Never change the indexing.** Do NOT reintroduce the old fork's arena / low-memory
   tiers / lazy-index / generation cache / abandon-build / per-turn cross-chapter
   prefetch. Every nicety is built on **CrossPoint's own indexing / section cache,
   exactly as they do it.**
2. **No sleep-wallpaper staging.** The old `SleepWallpaperStage` / `SleepWallpaperIndexStore`
   prewarm system starved button input. Wallpapers decode **on demand** only.
3. Match CrossPoint structure; keep the diff small so upstream merges stay easy.
4. Flash budget: plain CrossPoint 1.5.0 already uses ~83.6% of the 6.55 MB app
   partition. Watch it as fonts/features are added; drop unused upstream bits or
   trim font sizes if it approaches the limit.

## Niceties to port (ALL of them — Diogo keeps every extra)

Legend: [x] done · [~] in progress · [ ] todo

- [x] **PXC sleep wallpaper** — `.pxc` accepted for the sleep/lock screen (on-demand decode,
      no staging). Info-overlay (filename/favorite badge) + PXC viewer + unlock-banner reuse
      to be ported later.
- [x] **9 — Per-book reader settings** (each book its own look; global default + override + reset). Commit `094ef02a`.
- [x] **10 — Paragraph numbers** (per-book in-menu toggle; 3 states: off / per-chapter / whole-book). Commit `fd6bef6d`.
- [x] **Go to Paragraph** — reader-menu jump to a paragraph number (gated on numbering on; correct in both modes, whole-book converts via sectionParagraphCounts_). Commit `364c49f5`.
- [x] **Paperback Look** (heavier-ink double-strike smear) for reader body text + status
      bar; global default ON + per-book toggle. Commit `be2976d8`.
- [x] **First-line indent slider** — reader Layout setting (Text Settings + device Settings),
      per-book + global, 0–8 space-widths (default 3), applied to natural-aligned paragraphs
      with no CSS text-indent. `SECTION_FILE_VERSION` 33→34; `ReaderPrefs` VERSION 2→3. Commit `66b5e270`.
- [ ] **Word-spacing slider — FUTURE / deferred.** Skipped deliberately: it would spray a fixed
      addend across ~6 sites in the hottest layout code (line-break DP + justify spacer), a big
      diff in the exact file we keep matched to upstream, and the payoff is near-zero on justified
      text (the default) because justification already fills the line. Revisit only if left/ragged
      alignment becomes common. Threading path is identical to first-line indent if ever done.
- [x] **Vollkorn swap** — Vollkorn is now the ONLY built-in reading family + the default; Noto Serif
      and Noto Sans reading fonts dropped (users add more via SD-card fonts). Noto Sans survives only
      as the 8pt small font, Ubuntu as the UI font. Font IDs renamed NOTOSERIF/NOTOSANS → VOLLKORN
      (new hash values), so existing books re-lay-out once (Vollkorn metrics differ) with no section
      version bump. `FONT_FAMILY` enum collapses to `{ VOLLKORN }`; old saved family indices migrate
      to Vollkorn via the existing clamp. Baked scoped-to-Vollkorn only (4 sizes × 4 styles from the
      OFL variable font, sliced to static Regular/Bold/Italic/BoldItalic). Flash 83.5% → 70.3%. Commit `e1ae6e69`.
- [x] **Cozette UI font (language-conditional)** — Cozette is now the default menu font (Latin +
      Cyrillic + Vietnamese, verified in its cmap); Arabic + Hebrew UI fall back to Ubuntu (Cozette
      lacks those scripts). Active UI ids `UI_10/UI_12_FONT_ID` are REBOUND at boot + on every in-app
      language change (`removeFont`+`insertFont`; `bindUiFontsForLanguage` in main.cpp, declared in
      `UiFont.h`, called from `LanguageSelectActivity::handleSelection`). Ubuntu kept permanently under
      new `UBUNTU_10/12_FONT_ID`; the language-select list draws through it (new optional `itemFontId`
      param on `BaseTheme::drawList`) so Arabic/Hebrew native names never box. Baked Cozette 10/12
      reg+bold uncompressed (scoped to Cozette only; MIT licence in source/Cozette). No Korean/CJK UI
      exists in the firmware (31 languages, none CJK). Flash 70.4%→73.1%. Commit `ff087973`.
- [ ] Fonts/typography remaining: PT hyphenation; anti-alias fade off; paragraph-spacing slider;
      "Bionic Reading" name. (NotoSerif source TTFs left in-tree but unused — trim later if desired.)
- [x] **Home in-progress list** — recents as a list; full title WRAPPED (no truncation) +
      "by INITIALS" + inline `[NN%]` black-bg badge (via ported `BaseTheme::drawRecentBookList`
      + `wrapText`); "N more above/below" scroll indicators; cap 13; cover tile/thumbnail
      generation dropped (faster). Finished books auto-leave the list + move to /read
      (`removeReadBooksFromRecents` + `moveFinishedToReadFolder` now default ON). Commits `5b795243`, `d00b5c4f`.
- [x] **Home extras:** finished-book auto-file to /read + auto-remove from list (defaults ON).
- [x] **Touchscreen support REMOVED** — firmware is X3/X4 only (no touch). All touch/swipe/
      gesture code stripped end to end (input manager API, HAL wrappers, every activity, reader
      page-turn zones, keyboard cursor-tap, slider drags, clock touch buttons, option popup,
      settings/theme touch guards, `touchReaderControls` setting). Buttons only. Commit `32a9cff4`.
- [ ] Home extras remaining: "Opening…" banner, pages counter + clock, Pages button, cover/list toggle.
- [ ] Status bar v2 (placeable 6-anchor items, title wrap/reflow, TXT reader on it).
- [x] **Grab Quote** — reader-menu "Grab Quote" opens a button-only word-range picker on the current
      page (pick start word → pick end word → save). Saves `[chapter]\nquote\n---\n\n` to
      `<book>_QUOTES.txt` next to the book (atomic tmp/bak rotation, 24 KB cap). New standalone
      `QuoteSelectActivity` (modeled on `DictionaryWordSelectActivity`, NOT the old in-reader highlight
      mode). Pure helpers in `QuoteText.h` + `GrowthBounds.h` (host-tested, 8 tests). **v1 = single page
      only** (a quote must fit one page); cross-page selection + an on-device quotes browser + a "Saved"
      toast + a long-press trigger are all future. Commit `2932a5fb`.
- [ ] Reader menu tidy + chapter header (Grab Quote done above).
- [ ] Margins: uniform toggle + independent top/bottom.
- [ ] Sleep/boot: "Until Death" sleep screen, skull-crest boot logo (5 img) + segmented loader.
- [ ] UI polish: banner-style popups, throttled font-download progress.
- [ ] Misc: open-random-book-on-boot.
- [ ] WiFi file browser + OPDS-in-browser (was on a branch in the old fork).

**Dropped for good** (upstream has its own, usually better): custom `DisplayRefreshPolicy`,
sleep-staging internals, arena/tier cache, Rust helpers, our forked SDK panel fixes.

## Progress log

- **2026-07-23** — Stage 1: proved pure CrossPoint 1.5.0 builds for X3/X4. Stage 2: added
  `upstream` remote, created `crosspoint-rebase` worktree, pulled freeink-sdk, built in-repo.
- **2026-07-23** — Themes cut to a single "Lector" theme: removed Lyra / Lyra-3-Covers /
  RoundedRaff; kept CrossPoint "Classic" base, renamed enum `CLASSIC`→`LECTOR`; dropped the
  Settings theme picker. `BaseTheme`/`BaseMetrics` is the lector look canvas. Commit `02b81844`.
- **2026-07-23** — PXC sleep wallpaper ported. New CrossPoint-native
  `src/activities/boot_sleep/PxcSleepRenderer.{h,cpp}` (lean; mirrors `renderBitmapSleepScreen`'s
  3-pass grayscale pipeline — `displayGrayscaleBase`/`setRenderMode`/`copyGrayscale*Buffers`/
  `displayGrayBuffer` — feeding `.pxc` 2bpp pixels through the existing `DirectPixelWriter`).
  `SleepActivity::renderCustomSleepScreen` now accepts `/sleep.pxc` and `.pxc` files in the
  `/sleep` (or `/.sleep`) folder, branching to the PXC renderer by extension. On-demand decode
  only; NO staging (`SleepWallpaperStage`/`IndexStore` deliberately NOT ported). `hasPxcExtension`
  is a local inline in PxcSleepRenderer.h (keeps shared FsHelpers upstream-clean). Builds clean.
  NOTE: `.pxc` must be authored at exact panel size (Lector Wallpaper Converter output); the
  renderer rejects size mismatches and falls through to the next sleep screen.

- **2026-07-23** — Boot creates lector's SD folders on first install (idempotent
  `Storage.ensureDirectoryExists`, in `main.cpp` right after `Storage.begin()`):
  `/read` (opened books "move to read"), `/recents` (lector "move to Recents"),
  `/sleep` (wallpapers, .bmp/.pxc), `/sleep pause` (paused wallpapers — note the space).
  So a fresh SD is ready for drop-in without hand-creating folders. `READ_FOLDER="/read"`
  and `RECENTS_DIR="/recents"` (BookRelocation.h) are the canonical names.

- **2026-07-23** — Sleep wallpaper pick made O(1)-memory for huge folders. The old
  lector ordered playlist/index (`SleepWallpaperStage`/`IndexStore`/`WallpaperPlaylistV2`)
  was NOT ported (it rescanned `/sleep` on the reading loop — a slow input-starver). The
  CrossPoint base already picks random, but built a full `std::vector` of every filename
  AND parsed every BMP header per sleep — bad for Diogo's 2000–3000 image `/sleep`.
  Replaced with **reservoir sampling** in `SleepActivity::renderCustomSleepScreen`: one
  directory pass, keeps the k-th valid file with prob 1/k, holds only the winner, no
  per-file header read. Recently-shown avoidance dropped (pure random, per Diogo). `.pxc`
  + `.bmp` both eligible. Builds clean.

- **2026-07-23** — #9 per-book reader settings DONE (commit `094ef02a`). New
  `src/activities/reader/ReaderPrefs.{h,cpp}` (POD snapshot: font/size/lineSpacing/
  align/paraSpacing/margin/focus/hyphen/embedded/antiAlias/imageRendering/sdFont +
  reserved paragraphNumbering; `[version][POD]` serialization; host tests in
  `test/reader_prefs/`, 5, green). Reader holds `prefs_`/`prefsCustom_`, loads
  `<cachePath>/reader_override.bin` on enter (else `fromGlobal()`), and lays out
  exclusively through `prefs_` (added `CrossPointSettings::readerRenderSpec(w,h,prefs)`
  + `getReaderFontId(prefs)` overloads; refactored resolvers). In-book editor REUSES
  `TextSettingsActivity` via a guarded overlay in CrossPointSettings
  (`beginReaderEditOverlay`/`endReaderEditOverlay`; overlay-aware `saveToFile()`
  shadows the CRTP base so a book's values never reach settings.json). Menu rows
  `READER_SETTINGS` (always) + `RESET_READER_SETTINGS` (only when custom). Orientation
  stays GLOBAL (rotate is a device-level thing, not a per-book look). Device build
  83.6% flash (unchanged). NOT device-tested yet.

- **2026-07-23** — #10 paragraph numbers DONE (commit `fd6bef6d`). Parser
  (`ChapterHtmlSlimParser`) tags the FIRST line of each visible paragraph with a
  per-chapter ordinal (`paragraphOrdinal_` armed at `makePages`, consumed at
  `addLineToPage`); `PageLine` carries a `uint16 paragraphOrdinal`, (de)serialized in
  the section cache. **`SECTION_FILE_VERSION` 32 → 33** (partial sentinel auto-follows);
  pagination byte-identical, so indexing unchanged — the bump only forces the one-time
  rebuild that bakes the field. Mode = `ReaderPrefs.paragraphNumbering` (per-book);
  new reader-menu row "Paragraph Numbers" cycles Off / Per Chapter / Whole Book in
  place, applied on menu exit (`applyParagraphNumbering`; touch → book custom).
  `CrossPointSettings::PARAGRAPH_NUMBERING` enum = shared value type. Per-chapter =
  baked ordinal (base 0). Whole-book base = sum of prior spines' counts, captured
  render-side (`drawParagraphNumbers` records running max per spine) and persisted to
  a `paragraph_counts.bin` sidecar; finalizes as read forward. Numbers drawn with
  `SMALL_FONT_ID` left of `wordXpos(0)`, skipped if no margin room. Flash 83.7%.
  Device build clean; host reader_prefs 5/5. NOT device-tested yet.

- **2026-07-24** — Paperback Look ported (commit `be2976d8`). `GfxRenderer` gains a
  `mutable paperbackLook_` flag + `setPaperbackLook`/`getPaperbackLook`; the smear
  (re-plot each lit glyph pixel +1 right/+1 down) lives in `renderCharImpl`'s 2-bit and
  1-bit branches, **BW-guarded** so grayscale passes never thicken. Global defaults
  `CrossPointSettings::paperbackLookBody/Status` (=1, persisted manually in toJson/
  fromJson, not in the Settings screen); per-book override in `ReaderPrefs` (**VERSION
  1→2**, seeded from global). Two reader-menu rows ("Paperback Look" / "Paperback Status
  Bar") toggle like checkboxes, applied on exit via `applyPaperbackLook` (no re-index,
  ink weight only; carried across a Reader Settings edit like paragraphNumbering). EPUB
  reader brackets the BW body render + `renderStatusBar`; TXT/XTC readers bracket their
  draws with the GLOBAL flags (XTC = images, status bar only). Host test extended (still
  5, green). Flash 83.7%. NOT device-tested.

- **2026-07-24** — Go to Paragraph (`364c49f5`) + Home in-progress list (`5b795243`)
  shipped. Go-to-Paragraph: reader-menu row gated on numbering-on, reuses
  `KeyboardEntryActivity`; correct in both modes (whole-book converts via
  `sectionParagraphCounts_`, cross-chapter defers `pendingParagraphScan_` to the render
  path). Home: `RecentBook.progressPercent` (+ store `setProgress`, written on reader
  exit), `StringUtils::authorInitials` (host-tested), `HomeActivity` render swapped to
  `GUI.drawList` (title / "by INITIALS" / NN%); cover tile + thumbnail generation
  removed (home opens no book now). TODO: TXT/XTC don't write % yet.

- **2026-07-24** — Home list polish + finished-book auto-file (`d00b5c4f`): full title
  WRAPPED via ported `BaseTheme::drawRecentBookList` + `wrapText` (in BaseTheme.cpp; uses
  `StringUtils::authorInitials`), inline `[NN%]` black-bg badge (flips to white chip on the
  selected row), "N more above/below" indicators, cap 13, scroll state in HomeActivity.
  `removeReadBooksFromRecents`=1 + `moveFinishedToReadFolder`=1 defaults ON. Then home made
  button-only (`a9266014`).
- **2026-07-24** — **ALL TOUCH REMOVED** (`32a9cff4`, 56 files). Firmware is X3/X4-only.
  Stripped: `MappedInputManager` touch API + SwipeDir/RowTouch enums + touch-held state
  (wasPressed/Released/getHeldTime now button-only); `HalGPIO` 8 touch wrappers +
  `main.cpp` `wasTouchActivity`; `Activity` `handleListTouch`/`ListTouchResult`/
  `handleHomeGesture` + `ActivityManager` home-swipe dispatch; every activity's touch
  handler; `ReaderUtils` tap-zone helpers (`detectTouchPageTurn`/`isTouchMenuGesture`) +
  the reader page-turn merges; slider drags (percent/interval `draggingBar`); ClockOffset
  touch buttons; OptionPopup tap; KeyboardEntry cursor-tap/touchRouter loop; `touchReaderControls`
  setting/enum; BoardConfig::hasTouch gates (front-remap + OTA now always shown); theme
  hasTouch hint-suppression + metrics adjustment; i18n `STR_TOUCH_READER_CONTROLS`/`STR_TAP_TO_RETRY`.
  Touch DRIVER stays in freeink-sdk (never read). Nav = side Up/Down + the 2 rightmost front
  buttons (NavPrevious=Up+Left, NavNext=Down+Right — already mapped, no new wiring). Host 140/140,
  device build clean.

- **2026-07-24** — Session: first-line indent slider (`66b5e270`), Vollkorn swap (`e1ae6e69`, flash
  83.5%→70.3%), TXT progress % (`8be83e2f`), Grab Quote (`2932a5fb`), Cozette UI font
  (`ff087973`, flash →73.1%). Word-spacing deferred (future). Owed device tests: all of the above,
  esp. Cozette rendering + language-switch font rebind (try Arabic/Hebrew → Ubuntu, Russian → Cozette
  Cyrillic, Vietnamese → Cozette) + the language-picker native names not boxing.

## Next steps (RESUME HERE after compaction)

**Branch:** `crosspoint-rebase` (worktree `.claude/worktrees/crosspoint-base`), pushed to origin.
**Build:** `cd .claude/worktrees/crosspoint-base && pio run` (~30-55s). Host tests: `test/` (149/149). Flash 73.1%.
**Latest commits (newest first):** `ff087973` Cozette UI font, `2932a5fb` grab-quote, `8be83e2f` txt%,
`e1ae6e69` vollkorn, `66b5e270` first-line-indent, `32a9cff4` touch-removal, `a9266014` button-only home,
`d00b5c4f` home polish, `5b795243` home list, `364c49f5` go-to-paragraph, `be2976d8` paperback,
`fd6bef6d` #10, `094ef02a` #9, plus themes/PXC/folders/wallpaper.

**Font pipeline note:** Vollkorn + Cozette were baked in a Python venv at
`<scratchpad>/vollkorn/.venv` (fonttools + freetype-py). Ruby is absent on the box, so `fontIds.h`
was written by a Python re-implementation of `build-font-ids.sh`'s SHA256 formula (values are
content-derived, unique, nonzero — runtime only needs unique keys, so the pre-existing ubuntu-hash
"drift" is harmless). Source TTFs + licences committed under `builtinFonts/source/{Vollkorn,Cozette}`.

1. **First flashable test build + device test on X4** — bundle everything on `crosspoint-rebase`
   (Diogo said "can't flash now"; wait for his word before cutting a build). Owed device checks:
   per-book settings; paragraph numbers (3 modes); paperback look; home in-progress list;
   button-only nav incl. the 2 rightmost front buttons; **first-line indent slider**; **Vollkorn
   re-layout + look** (caches rebuild once, section v33→34); **TXT `[NN%]` badge**; **Grab Quote**
   (menu → pick start word → pick end word → confirm saves to `<book>_QUOTES.txt`; Back cancels/steps
   back); **Cozette menus + language-switch font rebind** (Arabic/Hebrew → Ubuntu, Russian → Cozette
   Cyrillic, Vietnamese → Cozette, and the language-picker native names must NOT box).
2. **Small follow-ups:** (a) TXT writes progress % (`8be83e2f`); comics/XTC intentionally do NOT.
   (b) KeyboardEntry still holds inert freeink `InteractionBuffer`/`TouchHoldRouter` scaffolding — trim.
   (c) home does not filter 100%-finished books (removal handles it at End-of-Book). (d) Grab Quote v1 =
   single-page only; cross-page + quotes-browser + "Saved" toast + long-press trigger = future.
   (e) NotoSerif source TTFs left in-tree but unused — trim later if desired.
3. **Remaining niceties:** status bar v2, margins, "Until Death" sleep screen, skull boot logo,
   open-random-on-boot, WiFi file browser + OPDS-in-browser, PXC info overlay / PxcViewerActivity,
   PT hyphenation, anti-alias fade off, paragraph-spacing slider, "Bionic Reading" name. No feature
   requested after Cozette yet — ASK Diogo what is next (or cut the test build).

**HARD CONSTRAINTS still in force:** never change the indexing (use CrossPoint's cache exactly);
no sleep-wallpaper staging; keep diffs small for upstream merges; NEVER `git add -A` (stage
tracked via `git add -u` or explicit paths); commit trailers required; auto-push after commit;
Caveman voice ("Rocky"), plain English for code/commits/warnings; call user "Diogo".
