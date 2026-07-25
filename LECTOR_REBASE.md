# Lector Re-base onto CrossPoint — Project Log

> Living document. Updated every step. This is the source of truth for the
> lector re-base so work can resume after any context compaction.
> Last updated: 2026-07-25 (lector.c 0.2.0 live on the flasher site).

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
      no staging). Grayscale on X3 **and** X4 (X4 needed the panel rails powered up before the
      gray LUT, `a5f33bef`). Unlock banners composite over the wallpaper on wake (`6742f344`).
      Still to port: info-overlay (filename / favorite badge), PXC viewer, favourites + pause folder.
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
- [x] **Granular reader settings** — the whole old-Lector reader data model restored and exposed in the
      tabbed Text Settings UI: independent + dynamic margins, paragraph spacing as a %, first-line indent
      Book/Custom%, and a reusable value-bar widget for every numeric row. Commits `b63215ff`, `9bd6e6b9`,
      `b78f5d19`, `d5a18b17`, `b0a32fc4`, `d0377549`. Live preview honours every layout setting (`09d6ca89`).
- [x] **Status bar v2** — per-item placement/customisation restored. Commit `6caefbfb`.
- [x] **Guide dots** — middle dot drawn between words. Commit `71cd7679`.
- [x] Fonts/typography: PT hyphenation added (trie + registry entry), text anti-aliasing defaulted OFF,
      "Bionic Reading" name restored. Word spacing stays deferred.
- [x] **Reading Themes** (from DX34) — named reader looks, applied per book. Code calls them PRESETS
      (`ReaderPresetStore`, `ReaderPresetsActivity`) because UITheme/BaseTheme already mean the menu
      chrome; the UI still says "Reading Themes". Deliberately CUT from the DX34 version: all 24
      status-bar fields and orientation (both are device settings here, so capturing them would make a
      theme write global state and break the per-book model), the one-level undo, the duplicate
      "adjust settings"/"reset to global" rows, the last-applied bookkeeping, and 16 slots down to 8.
      Stored as NAMED JSON KEYS, never a ReaderPrefs blob — the blob version is rejected hard on
      mismatch, so a field added to ReaderPrefs would have silently wiped every saved theme.
      `applyStolenLook` refactored into the shared `applyReaderPrefsFrom`, which also verifies the SD
      font is still installed and writes the correction back to the theme.
      (NotoSerif source TTFs left in-tree but unused — trim later if desired.)
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
- [x] **Opened books filed into /recents** on the way out. Commit `997758e2`.
- [x] Home extras: busy banner, pages counter + clock, firmware version label. Cover/list toggle dropped
      on purpose (Diogo, 2026-07-25) — thumbnail generation is what made the old home slow.
- [x] **Grab Quote** — reader-menu "Grab Quote" opens a button-only word-range picker on the current
      page (pick start word → pick end word → save). Saves `[chapter]\nquote\n---\n\n` to
      `<book>_QUOTES.txt` next to the book (atomic tmp/bak rotation, 24 KB cap). New standalone
      `QuoteSelectActivity` (modeled on `DictionaryWordSelectActivity`, NOT the old in-reader highlight
      mode). Pure helpers in `QuoteText.h` + `GrowthBounds.h` (host-tested, 8 tests). **v1 = single page
      only** (a quote must fit one page); cross-page selection + an on-device quotes browser + a "Saved"
      toast + a long-press trigger are all future. Commit `2932a5fb`.
- [x] **Reader menu header** — title / "by author" / chapter / "page/pages | Book %". Commit `edd1c533`.
- [x] **Margins** — uniform toggle + independent sides, plus dynamic margins. Commit `9bd6e6b9`.
- [ ] Sleep faces: the rebase has 9, the old fork 13. Missing: `UNTIL_DEATH` (random logo),
      `RANDOM_LOGO_CUSTOM`, `STATS_DASHBOARD_PLUS`, `QUOTES`. FREEZE is DONE.
- [ ] Boot: skull-crest logo (5 img) + segmented loader. Open-random-book-on-boot is DONE.
- [x] Reader menu: Steal Look and View Quotes — DONE. QR share was never missing.
- [ ] UI polish: throttled font-download progress. (Banner-style popups are done — every popup is now
      one full-width black strip, `BaseTheme::drawBannerStrip`.)
- [ ] WiFi file browser (old fork branch, 5-phase plan). OPDS-in-the-web-page is CUT (Diogo,
      2026-07-25) — the web pages were restyled black and white instead.
- [x] Wallpaper management: favourites + `/sleep pause` + bulk move-by-favourite + info overlay +
      `PxcViewerActivity` + reader-menu Delete + BMP-viewer triage + Sleep Image Quality.
- [x] **Clean Up Storage** — Settings action that removes only the cache directories whose book is gone,
      leaving every present book's progress alone. Commit `11d98cea`.
- [x] **Book Info** — reader-menu screen with cover, title, author, language and a paged synopsis. Required
      plumbing `<dc:description>` through the OPF parser into `book.bin` (version 8 → 9). Commit `d1a7f8d3`.
- [x] **Reading Stats** — engine + per-book/all-books screen + two System settings; all three readers feed
      the tracker, only the EPUB menu opens the screen (TXT/XTC have no reader menu here). Commit `8a939fac`.

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

- **2026-07-24 — released `lector.c 0.0.4`** (`0fb7f514`). The big reader-settings restoration, in
  batches: data model (`b63215ff`), independent + dynamic margins and debug borders (`9bd6e6b9`),
  granular paragraph spacing % (`b78f5d19`), first-line indent Book/Custom% (`d5a18b17`), value-bar
  widget wired to the numeric rows (`b0a32fc4`), per-item status bar v2 (`6caefbfb`), every reader
  setting exposed in the tabbed Text Settings UI (`d0377549`), guide dots (`71cd7679`), wake/unlock
  banners with an editable footer (`d3b744a1`), and the **X4 grayscale sleep fix** (`a5f33bef`) — the
  panel rails must be powered up before the gray LUT is loaded, which retires the earlier 1-bit X4
  fallback. `pxcGrayscale` is now `true` on both panels and the temporary `[SLP]` markers are gone.

- **2026-07-24 — released `lector.c 0.0.5`** (`997758e2`). Menu chrome lightened, page count labelled,
  Refresh Frequency gains "Never" (`a3afaef8`); UI bold sweep finished (`6ec99797`) and then the bold UI
  font cuts dropped for **~410 KB of flash** (`66e2484a`); sleep-wallpaper folder scan yields to the
  watchdog (`35e4cf60`) and picks by seeking to a random directory slot instead of walking (`0db423eb`);
  the host-side wallpaper rename script became one command doing all three steps, with incremental map
  writes, `--compact`, `--lowercase`, and macOS-metadata stripping (`710c9fa1`, `ebef0668`, `bd9c8aa8`,
  `bd7253e2`); opened books file into `/recents` on the way out (`997758e2`).

- **2026-07-25 — released `lector.c 0.0.6`** (`1fca734e`). Wake banners now show on **every** wake and
  hold for a beat (`43e6818a`); file browser wraps long names instead of ellipsising (`c05bc02b`); the
  reader-settings live preview honours every layout setting (`09d6ca89`); a child screen's Back *release*
  no longer throws the user out of the book (`65244e60` — the press/release split-click class).

- **2026-07-25 — released `lector.c 0.0.7`** (`b216bd31`). Two device-reported bugs:
  1. **Unlock kept the boot logo instead of the wallpaper** (`6742f344`). 0.0.6's banners were drawn by
     `BootActivity`, which paints `Logo120`; a wallpaper sleep resolves to `BootResume::Splash`, so the
     banners landed on the logo. Deep sleep is a chip reset, so the wake cannot know what the panel holds
     unless it is written down: `CrossPointState::lastSleepWallpaperPath` now records the wallpaper the
     sleep screen actually rendered, `main.cpp` starts the display **seamless** on that wake, and
     `BootActivity` re-renders the `.pxc` 1-bit with the banners passed in as an overlay function pointer
     so wallpaper + banners land in ONE refresh (no white flash). `.pxc` only; other formats still show
     the logo screen.
  2. **Per-book font size did not survive reopening a book** (`342cd4e6`). The per-book file was always
     correct — which is why the settings screen kept showing the right value. `SdCardFontSystem` keeps
     exactly **one** resident SD font size, chosen from the GLOBAL setting, and `resolveFontId()` returns
     that resident id whatever size is asked for. Since the id is part of the section cache key, pages
     built at the book's size were discarded and rebuilt at the global size on every open. New
     `ensureLoadedFor(family, sizeEnum)` makes a book's own family+size resident before it lays out;
     unlike `ensureLoaded()` it never clears the global selection on a missing family.
     **Durable trap: the font LOAD is the authority, not `resolveFontId`.** Built-in Vollkorn was never
     affected (the SD branch is skipped), so this bug only ever bit SD-card fonts.

- **2026-07-25 — sleep-face ghosting fixed (unreleased).** Device photo: a `.pxc` wallpaper with the
  Text Settings menu reading through it for the whole lock. Cause: every sleep face paints with a
  differential waveform (HALF for logo/blank/bitmap, graybase + gray-nudge LUT for the grayscale
  wallpaper pipeline), and a differential only transitions changed pixels, so the prior screen survives
  underneath. The "Entering sleep" popup does not clean it either — `drawPopup` ends in a FAST_REFRESH,
  also differential. Old lector solved this in v0.37.0 with `deepCleanPanel()`; the rebase's lean sleep
  port never carried it. New `SleepActivity::deepCleanPanel()` (clearScreen + `FULL_REFRESH`) runs once
  in `renderSleepScreen()` after the popup and before the face switch, so it covers custom / cover /
  default / blank in one place. Quick-resume returns earlier and is deliberately NOT cleaned — that face
  must keep the frame it inherits. Costs ~1.5 s and the multi-flash GC waveform upstream avoids for
  sleep (#2471), but sleep is already committed at that point.

- **2026-07-25 — three old-lector screens ported (unreleased).** Diogo picked the "book screens" group.
  1. **Clean Up Storage** (`11d98cea`). Sits above Clear Reading Cache in Settings. Deletes only
     `/.crosspoint` cache directories whose book is no longer on the card. It enumerates every book first
     and deletes NOTHING unless that walk completed, because a book the walk missed is indistinguishable
     from an orphan and its cache holds that book's progress. Live books are kept as 16-byte keys
     (prefix + `std::hash` of the path) in a sorted vector, not a set of name strings — a few hundred books
     as strings would cost hundreds of KB of a 380 KB heap; the 2000-book cap bounds it at 32 KB. The walk
     is iterative with an explicit stack and feeds the watchdog every 256 entries; the delete loop feeds it
     too, because the rebase's `SDCardManager::removeDir` recurses a whole tree without feeding it (the old
     fork's did — the existing Clear Cache screen still has that hole).
  2. **Book Info** (`d1a7f8d3`). Second row of the reader menu. The screen itself was a near-verbatim port;
     the work was the synopsis, which the rebase did not store at all. `ContentOpfParser` now captures
     `<dc:description>` (stopping at 1500 bytes while appending, not trimming after), `Epub` flattens the
     HTML publishers put in there once at cache-build time, and it is written to `book.bin`.
     **`BOOK_CACHE_VERSION` 8 → 9**, so every book re-reads metadata/spine/TOC once on next open; laid-out
     pages and progress are untouched. The size estimate's length-prefix term went 5 → 6 strings — it feeds
     `lutOffset`, so missing it would have pointed the lookup table four bytes short.
  3b. **Stats Dashboard sleep screen** (`878ffba5`). New Sleep Screen option: the book's cover with a stats
     overlay (reading time, time left, progress, daily average, pages/min, days reading, finish estimate,
     title + chapter, streak, reader-type label). Falls back to the default face when there is no open book,
     no stats for the format, or no cover. Appended as `SLEEP_SCREEN_MODE` value 7 — the stored value IS the
     persisted setting, so faces must only ever be appended; a `static_assert` pins it to the renderer's own
     constant. The grayscale sequence mirrors `renderBitmapSleepScreen` (base stays HALF) and the face does
     NOT clean the panel, because `renderSleepScreen` already does. The "plus" wallpaper variant and the
     renderer's PXC branch were NOT ported: they need the dropped wallpaper playlist, and the cover path
     only ever yields BMP.
  3. **Reading Stats** (`8a939fac`). Engine (`src/reading_stats/`, 12 files) + `BookStatsActivity` +
     `readingStatsEnabled` / `readingStatsIdleUnits` under System. Two 180-byte files, one per book in its
     cache dir and one global at `/.crosspoint/global_reading_stats.bin`, saved via verified temp + `.bak`
     rotation. All three readers feed the tracker so TXT and comics count toward all-books totals; only the
     EPUB menu opens the screen because TXT/XTC have no reader menu in the rebase. Tracking is latched at
     open so a mid-book toggle cannot half-track a session; backward turns close the page out rather than
     credit it; the page timer starts when the page reaches the panel, not at the press.
     **`HalClock::getDateTime()` is new** — streaks, weekday buckets and start/finish dates need a calendar
     date and the HAL only exposed hour/minute. It reads the SDK RTC and falls back to the system clock on
     boards without one, rejecting an unset clock rather than recording a reading day in 1970.

- **2026-07-25** — **Wallpaper management ported** (option "A"), five commits. The whole group is
  state-free by design: nothing new is persisted, because on a folder of thousands of images any
  separate index or favourites list is one more thing that can drift out of sync with the files.
  1. **Model** (`b9093fc4`). `src/util/FavoriteImage{,Names}` — a wallpaper is a favourite iff its
     basename carries the `_F` suffix, so the state survives reboots, SD edits and card swaps.
     `src/sleep/SleepPauseToggle` — `"/sleep pause"` is an ordinary folder the sleep screen does not
     read from, so the file's location IS its rotation state. `src/sleep/SleepImageMove.h` — the bulk
     mover, in bounded passes: stream the folder, retain at most `batchSize` names, rename that batch,
     repeat. Peak heap is one batch, not the folder listing, which is the only reason a 3000-image
     folder is safe here. A pass that matches files but moves none stops the run, so a permanently
     un-movable file cannot spin forever. `isWallpaperName` moved out of `SleepActivity.cpp` into
     `src/sleep/WallpaperNames.h` and now delegates to the same extension list the favourite toggle
     uses — sleep screen, toggle and mover can no longer disagree about what a wallpaper is.
     33 host tests in `test/sleep_favorites/` (mover runs against an in-memory fake).
  2. **Sleep info overlay** (`2208c245`). Display toggles "Show Wallpaper Name" / "Show Favorite
     Badge". **`renderPxcSleepScreen`'s `overlay` hook now fires once per pass** (BW base + LSB + MSB),
     not only on the 1-bit path — a plane pass only carries the pixels written during that pass. The
     overlay fills its black box in every pass but draws white text/border only in the BW pass, or the
     1-bit glyph path sets both plane bits and the text comes out dark grey. Wake banners unaffected
     (`BootActivity` renders 1-bit = one call). The hook has no context parameter, so the path reaches
     the overlay through a `SleepInfoOverlayScope` guard, not a bare global.
  3. **Reader menu** (`db956c03`). "Favorite Wallpaper" / "Pause This Wallpaper" rows appear high in
     the menu when the last sleep screen was a wallpaper still on the card — the wake→menu→triage flow.
     Pause is hidden for a fixed `/sleep.pxc`, which has no rotation folder to leave.
  4. **On-device viewer** (`9ee64d61`). `.pxc` now lists in the file browser (`[F] name` for
     favourites) and opens `PxcViewerActivity`: Back / Fav / Delete / Pause, Up-Down to step the folder.
     Stepping uses `src/sleep/WallpaperNeighbour.h`, a streaming lexicographic lookup that retains one
     candidate name — `BmpViewerActivity`'s sorted sibling vector would be one `std::string` per file
     and is fatal here. Preview renders **1-bit, not 3-pass grayscale**: one pass is ~a third of the
     panel time (what makes stepping usable) and the single overlay call composites the button hints
     into the same refresh. Deep-cleans first or the browser list ghosts through.
  5. **Bulk moves** (`7aaf2bc4`). Display actions Pause Favorites / Pause Others / Restore Paused.
     Counts first (capped, so the prompt is prompt on a huge folder; past the cap it says "or more"),
     confirms with the number, then moves; reports moved and stuck counts. Restore runs the favourites
     pass then the non-favourites pass so every bulk move uses the one tested batching path.

  NOT ported from the old fork: the rotation-pause *flag* (the rebase picks a random wallpaper every
  sleep — there is no cursor to pause), and the `ISleepFs` playlist interface it hung off.

- **2026-07-25** — **Home extras** (option "B"), two commits.
  1. **Busy banner** (`fe5a5eee`). One shared strip at the top with short non-bold text, replacing the
     silence over several multi-second waits. **It is late, not eager**: there is no regional refresh
     wired up (`HalDisplay` only refreshes the whole panel; the SDK's `displayWindow` reaches
     `GfxRenderer.h` as a commented-out declaration and nothing calls it), so drawing costs a real
     refresh and showing it eagerly would slow the quick cases for nothing. `BusyBanner` arms; slow
     loops call `busy::tick()` where they already feed the watchdog; the strip appears only past 400 ms.
     Always-slow steps (SD font parse, first-open index build) call `busy::tickNow()` and skip the wait.
     The hook is a bare function pointer in `src/util/BusyTick.h` so scanning code in `util/` keeps no
     dependency on the display stack. Banners nest (open book → load font → build index). Covered:
     file-browser scans (armed inside `loadFiles`, so all seven call sites get it), Settings open (it
     rescans SD fonts + dictionaries first), SD font load, first-open EPUB indexing, wallpaper folder
     stepping, end-of-book "read next". NOT converted: the network activities' full-screen status text,
     and `EpubReaderActivity`'s mid-render chapter-build popups (they paint from inside `render()` under
     the `RenderLock`).
  2. **Pages tally + clock** (`0702e2ef`). `APP_STATE.sessionPagesRead`, incremented in all three
     readers at the same point reading stats count a page. Header tile plus the clock left of the
     battery (RTC boards only). The tile is **last in the tab order** although it is drawn first: as
     index 0 it would be the home screen's opening selection, and the first Confirm after a wake would
     zero the count instead of opening a book.

  Not done from the old "home extras" list: the cover/list toggle (the rebase home already lists
  recents with `[NN%]` badges) and the old home's `SdFileIndex` + library search, which is a separate
  ~1700-line port, not an "extra".

- **2026-07-25** — **Wallpaper hold** (`8a85e2d0`), the rotation-pause flag after all, at Diogo's
  request. `SETTINGS.wallpaperRotationPaused`: Display toggle "Hold Wallpaper" plus a reader-menu row
  that flips it in context. While held, the folder rotation keeps showing the wallpaper already up.
  Honoured only while that exact file is still in that folder — favouriting renames it, pausing moves
  it out, the browser can delete it — so a stale name falls through to a normal random pick instead of
  a blank sleep screen. `SleepActivity` now keeps the previous path in a member, because `onEnter`
  clears the shared `APP_STATE` field before rendering. No extra SD writes: the rendered path equals
  the stored one, and `onEnter` only saves on change.

## Next steps (RESUME HERE after compaction)

**Branch:** `crosspoint-rebase` (worktree `.claude/worktrees/crosspoint-base`), pushed to origin.
**Build:** `cd .claude/worktrees/crosspoint-base && pio run` (~30-55s). Host tests: `test/` (149/149).
**Sizes at 0.2.0:** `default` RAM 16.0% / Flash 73.3%; `gh_release` `firmware.bin` 4,777,536 bytes.
**Live on the flasher site: `lector.c 0.2.0`** (published 2026-07-25, firmware.bin 4,777,536 bytes; bootloader/partitions/boot_app0 byte-identical since 0.0.10 and left in place). Nothing is built-but-unreleased.

**0.2.0 = the first upstream merge on this branch.** `git merge upstream/develop` brought nine commits
(upstream 1.5.0) and moved the `freeink-sdk` pointer to `ae68356`. Eight conflicts; the settlements are
recorded in the merge commit message, and the rule that decided every one of them was: OUR feature stays,
THEIR mechanism is folded around it. Notably `main.cpp` quick resume keeps our wake banners as the loading
face but paints them through upstream's X3 differential path (#2698), and `EpubReaderActivity` takes their
`manualRefreshPending` while still reading `textAntiAliasing` from the book's own prefs.
Plus our own fix: paragraph numbering skips h1-h6 blocks and blocks with no letter or digit
(`ParsedText::hasLetters`, `ChapterHtmlSlimParser::currentBlockIsHeading_`).
`SECTION_FILE_VERSION` 37 -> 39 (38 = ruby, 39 = paragraph ordinals): each chapter re-lays out on first
open. NOT host-tested — this branch has no ParsedText test harness; device test owed.

**0.1.0 contents:** Portuguese hyphenation; text anti-aliasing default off (no grey fade per turn);
Reading Themes (`ReaderPresetStore`, 8 named slots, JSON named keys never a `ReaderPrefs` blob);
pages-this-session status bar item (`sbSessionPagesPos`, drawn `+12`); the status-bar clock SETTING row
hidden when `halClock.isAvailable()` is false (the clock item itself was already RTC-gated); one shared
banner look (`src/components/BannerStyle.h` — UI_10, 10px pad, 2px bottom rule, black fill from row 0 so
nothing white shows above it) with `drawOptionPopup` / `ValueBarPopup` reverted to a white panel, since
black is for the banner only.

**RELEASE RULE (Diogo, 2026-07-24):** NEVER publish to the flasher site (`lector-xteink-firmware`) without
Diogo's explicit OK — the site push itself can auto-deploy Pages. Never flash the device without asking.

**Publish path (manual — this branch inherited upstream's `release.yml`, which has no publish robot):**
1. Bump `[crosspoint] version` in `platformio.ini`, commit, push.
2. `pio run -e gh_release`; confirm the baked string with
   `strings .pio/build/gh_release/firmware.bin | grep 'CrossPoint version'`.
3. In the site clone `~/projects/lector-xteink-firmware`: `cmp` `bootloader.bin` / `partitions.bin`
   against `flash/firmware/latest/` and leave them alone if identical; copy in the new `firmware.bin`.
4. Stamp `flash/version.txt`, `flash/manifest-full.json`, `flash/manifest-update.json`; rewrite BOTH
   "What's new" blocks in `index.html` (the update panel and the standalone whatsnew panel).
5. `node --test` in the site repo, stage explicit paths only (never `git add -A`), commit, push.
6. Poll live `flash/version.txt` until it serves the new version; check `content-length` on the binary.

**Font pipeline note:** Vollkorn + Cozette were baked in a Python venv at
`<scratchpad>/vollkorn/.venv` (fonttools + freetype-py). Ruby is absent on the box, so `fontIds.h`
was written by a Python re-implementation of `build-font-ids.sh`'s SHA256 formula (values are
content-derived, unique, nonzero — runtime only needs unique keys, so the pre-existing ubuntu-hash
"drift" is harmless). Source TTFs + licences committed under `builtinFonts/source/{Vollkorn,Cozette}`.

### 1. Owed device tests (nothing here is hardware-verified)

Everything since 0.0.1 was built, host-tested and shipped, but **not** device-tested. Highest value first:
- **Unreleased:** locking from any screen leaves NO ghost of that screen under the sleep face.
- **Unreleased (wallpapers):** Display · Show Wallpaper Name puts the filename bottom-left on a `.pxc`
  sleep face without greying the text; reader menu · Favorite Wallpaper renames the file and the next
  wake still shows it under the banners; file browser lists `.pxc` and opens the viewer; Up/Down steps
  the folder at a usable speed on the real 2000-3000 image `/sleep`; Display · Pause Favorite Wallpapers
  states a count, moves that many, and the sleep rotation stops showing them.
- **Unreleased (home extras):** the busy banner appears on a big folder and on opening Settings, and
  does NOT appear (no extra flash) on a small folder — that "quick stays quick" check is the one that
  matters, since the banner costs a full-panel refresh. Home shows "Pages N", the count rises as you
  read, and selecting the tile (it is the LAST item in the tab order, after Settings) zeroes it.
  On X3 only: the clock shows left of the battery.
- **Unreleased:** Settings · Clean Up Storage removes orphan caches only (delete a book, sweep, confirm the
  other books still open at their saved place); reader menu · Book Info shows cover/author/language/synopsis;
  reader menu · Reading Stats counts a session, and the numbers survive closing and reopening the book.
  Settings · Display · Sleep Screen · Stats Dashboard sleeps to the cover with stats over it (and falls back
  to the logo face when there is no open book).
  Reading Stats needs the clock set, or streaks and weekday buckets will be wrong.
- **0.0.7:** unlock keeps the `.pxc` wallpaper with banners on top (no logo, no white flash); a book's own
  font size survives closing and reopening it (SD-card fonts only — the bug never touched Vollkorn).
- **0.0.4/0.0.5:** X4 grayscale `.pxc` sleep (the rails fix — X4 should now show 4-level gray like X3, not
  1-bit dither, and must not freeze on "Entering sleep"); the full reader-settings tab set; status bar v2;
  guide dots; wallpaper pick speed on the 2000-3000 image `/sleep` folder.
- **Older, still owed:** per-book settings, paragraph numbers (3 modes), paperback look, home list,
  button-only nav incl. the 2 rightmost front buttons, first-line indent, Vollkorn look, TXT `[NN%]`,
  Grab Quote, Cozette menus + language rebind (AR/HE → Ubuntu, RU/VI → Cozette).
- Still owed off-device: run the wallpaper rename script on the **X3** card
  (`python3 ~/Downloads/rename_wallpapers.py "/Volumes/X3CARD/sleep" --apply`, optionally `--lowercase`).

### 2. Small follow-ups

(a) TXT writes progress % (`8be83e2f`); comics/XTC intentionally do NOT.
(b) `KeyboardEntryActivity` still holds inert freeink `InteractionBuffer`/`TouchHoldRouter` scaffolding — trim.
(c) Home does not filter 100%-finished books (removal handles it at End-of-Book).
(d) Grab Quote v1 = single-page only; cross-page + quotes-browser + "Saved" toast + long-press = future.
(e) NotoSerif source TTFs left in-tree but unused — trim later if desired.
(f) `CrossPointState::recentSleepImages` / `pushRecentSleep` / `isRecentSleep` are dead code here (no caller
    outside the class) — they belong to the dropped recently-shown avoidance. Remove or wire up.
(g) `readerEditOverlayActive_` can leak if `TextSettingsActivity` is popped by a stack-clearing
    `Replace`/`goHome` instead of `finish()`. Not on any known path, and `saveToFile()` still protects
    `settings.json`, but it is a real hole.

### 2b. Session 2026-07-25 (after 0.0.8): popups, recents, browser

Seven commits, all built (`pio run -e default` green, Flash 72.7%) and host-tested (217/217,
28 new). Nothing released, nothing flashed.

- **One popup look.** `BaseTheme::drawBannerStrip` paints a full-width black strip with a white
  inset border and white centered text; `drawPopup` adds the full refresh, `BusyBanner` the cheap
  FAST one. `drawOptionPopup` and `ValueBarPopup` moved onto the same surface (selected row = white
  chip, black text). Dropped the dead `popupTopOffsetRatio` / `popupTextBold` / `popupTextInverted`
  metrics and flipped the popup progress bar to white.
- **Unlock banners.** Footer was `UI_12` against the title's `UI_10` — same weight, different size,
  which read as bold. Both `UI_10` now. The version line dropped its "Lector " prefix (the version
  string already names the firmware, so it read "Lector lector.c 0.0.8").
- **Remove from Recents, from inside the book.** New reader-menu row: drops the entry, moves the
  file back from `/recents` to the card root, clears the resume pointer, leaves the book. The move
  runs in `onExit` where the Epub is already released. The Recent Books screen's own remove follows
  the same rule now.
- **`src/util/BookFiling{,Names}`** — the filing helpers left `EpubReaderActivity`'s anonymous
  namespace so both callers share them. Path arithmetic is pure and host-tested. Fixes a latent bug:
  the cache re-key hardcoded the `epub_` prefix, but TXT and XTC use `txt_` / `xtc_`, and the Recent
  Books screen holds those too.
- **Home version label** back at the header's left edge, Pages tile shifted right.
- **File Browser Order** (Settings · System): Alphabetical or Random. Fisher-Yates over the file
  tail via `esp_random()`; folders stay sorted on top; fresh shuffle per folder open; Books mode only.
- **Wallpaper triage completed.** Delete Wallpaper on the reader menu (last row, behind a
  confirmation; deleting the held wallpaper resumes rotation). `BmpViewerActivity` gained the
  `.pxc` viewer's triage inside sleep folders (favourite / delete / move to pause, siblings on
  Up/Down); outside them Confirm still sets the sleep cover. Sleep Image Quality (Display) now
  drives the grayscale flag `renderPxcSleepScreen` already took: Pretty = 3-pass OEM, Fast = one
  1-bit pass.
- **File browser search.** `src/activities/home/LibrarySearch.{h,cpp}`, pure + host-tested: three
  tiers (name prefix, word-start, in-order-with-gaps), tighter match wins, ties keep listing order.
  Reached through synthetic "Search this folder" / "Clear search" rows above the entries, so no new
  button. Current folder only; cleared on navigation. NO live preview (a keystroke would cost a full
  keyboard repaint on e-ink). The browser list is addressed by ROW now, not by file index —
  `findEntry` became `findEntryRow`.

Shipped as **lector.c 0.0.9** (2026-07-25). Device test owed on all of it. Cover/coverflow home layout was considered and **dropped on purpose**
(Diogo, 2026-07-25): thumbnail generation is what made the old home slow, and the list already
carries title + `[NN%]`.

### 2c. Session 2026-07-25 (part two): OPDS, sleep ghost, five ports

- **Sleep wallpaper ghosting FIXED** (`ba41d8c7`). Device test isolated it: locking after a cold boot
  is clean, locking after use ghosts — so the blank and the driver are fine, and accumulated charge is
  the cause. The old fork bounded it with `DisplayRefreshPolicy` (every 13th consecutive FAST promoted
  to a clean refresh); that policy stays dropped, because capping FAST puts a flash back into reading
  and Refresh Frequency's "Never" exists to prevent exactly that. `deepCleanPanel` now drives the panel
  to black and back to white, two FULL passes. The black pass does the work. Also stops painting the
  "Entering sleep" popup when the lock goes straight to a wallpaper (the old fork's directWallpaperLock).
- **Built-in OPDS server** (`8b8c21f7`): `OpdsServerStore::seedBuiltInServers()` adds the shipped entry
  once, guarded by `builtins_seeded` in opds.json — a deleted server stays deleted. NAME AND URL ONLY.
  **Never compile credentials in**: this firmware is published as a public binary. The full `https://`
  scheme is spelled out because `UrlUtils::ensureProtocol` defaults a bare host to `http://`, which is
  what made adding it by hand fail against an HTTPS-only port.
- **Open Random Book on Boot** (`bf471325`), **Bionic Reading** name (`ef373d5b`), **Freeze sleep face**
  (`36ab41fe`, appended as mode 8 + `sleepFrameColor`; also joins `isQuickResumeSleep` or it wakes to
  Home), **Steal Look** (`6f016d1f`, one-time snapshot; orientation deliberately NOT copied since it is
  a device setting here), **View Quotes** (`d2be59c3`, `drawWrappedList`, delete via ConfirmationActivity).
- **Web pages strictly black and white** (`a310924b`): all four under `src/network/html/` (NOT
  `data/html/`, which does not exist here). Verified in headless Chromium against a mocked device API.
- `[NN%]` chip spacing evened for real: `getTextWidth` returns an INK box whose left edge is clamped to
  the pen, so the opening bracket's side bearing is inside the width while the right edge stops on the
  last lit pixel. The trailing blank is 0, not the advance's leftover.

Shipped as **lector.c 0.0.10** (2026-07-25).

**Device results (Diogo, 2026-07-25):** OPDS works — connected and downloaded a book, so the built-in
server and the Basic-auth path are hardware-verified. **The sleep ghost is NOT fixed** and is parked at
Diogo's request. What is now known, so nobody repeats it:
- The black-then-white wipe genuinely runs (he watched it), so the FULL refresh is real and the SDK's
  `0xF7` sequence and the FULL-to-HALF demotion path are both cleared as suspects.
- A ghost that survives a complete inversion cycle is not surface ghosting; it is deep trapped charge
  from content held still for minutes.
- The cold-boot lock being clean does NOT exonerate the wipe — a cold panel has nothing held long
  enough to trap charge. (An earlier note in this log over-claimed that; corrected here.)
- **His Refresh Frequency is "Never".** That is the charge source, and the trade is real: zero flashes
  while reading means the panel holds charge and the lock cannot fully undo it.
- Untried next steps, in order: run three black/white cycles at lock instead of one (~4 s, hidden); or
  reinstate a FAST cap outside the reader only, leaving reading flash-free.

### 3. Remaining niceties still to port from old lector

Grouped by theme (see the checklist above for the full list):
- ~~**Wallpapers:** favourites + `/sleep pause` + move-by-favourite bulk actions, filename/favorite info
  overlay, `PxcViewerActivity`~~ — DONE (`b9093fc4`, `2208c245`, `db956c03`, `9ee64d61`, `7aaf2bc4`).
- ~~**Book screens:** `BookInfoActivity`, Reading Stats, `CleanStorageActivity`~~ — DONE (`11d98cea`,
  `d1a7f8d3`, `8a939fac`), plus the Stats Dashboard sleep face (`878ffba5`).
- ~~**Home extras:** "Opening…" banner, pages counter + clock, Pages button, cover/list toggle~~ — DONE
  (`fe5a5eee`, `0702e2ef`); the cover/list toggle was not needed (the rebase home already lists recents
  with `[NN%]` badges). Still outstanding and NOT an "extra": the old home's `SdFileIndex` + library
  search (2765 lines against the rebase's 1064) is a separate, much larger port.
- ~~**Wallpaper triage tail:** reader-menu Delete, BMP-viewer sleep actions, Sleep Image Quality~~
  — DONE 2026-07-25. Still absent by choice: `wallpaperFormat` (the browser lists `.bmp` AND `.pxc`
  unconditionally, which is better), and `sleepFrameColor`, which belongs with the Freeze sleep face.
- ~~**File browser:** in-folder search, random order, `.pxc` viewer~~ — DONE (`.pxc` viewer was
  already ahead of the old fork). The old fork's `SdFileIndex` external-merge index was NOT ported:
  search ranks the in-RAM listing, which is what the rebase browser holds.
- **Sleep/boot:** "Until Death" sleep screen, skull-crest boot logo (5 img) + segmented loader,
  Freeze sleep face (+ `sleepFrameColor`), Quotes sleep face.
- **Networking:** WiFi file browser + OPDS-in-browser (was a branch in the old fork; a full 5-phase plan
  already exists, including the path-traversal hardening and the PIN gate).
- **Typography:** PT hyphenation, anti-alias fade off, "Bionic Reading" name, word-spacing slider (deferred).
- **Misc:** open-random-book-on-boot, banner-style popups, throttled font-download progress.
- **DX34 leftovers:** Reading Themes, Dark Mode and the rest of the dropped-features survey
  ([[project_lector_dx34_dropped_features]]).

**HARD CONSTRAINTS still in force:** never change the indexing (use CrossPoint's cache exactly);
no sleep-wallpaper staging; keep diffs small for upstream merges; NEVER `git add -A` (stage
tracked via `git add -u` or explicit paths); commit trailers required; auto-push after commit;
Caveman voice ("Rocky"), plain English for code/commits/warnings; call user "Diogo".
