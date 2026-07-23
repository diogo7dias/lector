# Lector Re-base onto CrossPoint — Project Log

> Living document. Updated every step. This is the source of truth for the
> lector re-base so work can resume after any context compaction.
> Last updated: 2026-07-23.

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
- [ ] **9 — Per-book reader settings** (each book its own look; global default + override + reset).
- [ ] **10 — Paragraph numbers** (TOP PRIORITY feature; per-book, in-book toggle; **keep 3 states: off / per-chapter / whole-book**).
- [ ] Fonts/typography: Bookerly, Georgia, Verdana, Merriweather (11–16); Cozette UI;
      PT hyphenation; Paperback heavier text; anti-alias fade off; first-line indent;
      word-spacing + paragraph-spacing sliders; "Bionic Reading" name.
- [ ] Home: list layout (`homeLayout`), `[NN%]` badge, "Opening…" banner, pages counter + clock, Pages button.
- [ ] Status bar v2 (placeable 6-anchor items, title wrap/reflow, TXT reader on it).
- [ ] Reader menu tidy + chapter header + **Grab Quote** (`<book>_QUOTES.txt`).
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

## Next steps

1. **#9 — per-book reader settings** (on CrossPoint indexing) — NEXT (after Diogo's compaction).
2. #10 paragraph numbers (3 states) on top of #9.
3. Continue down the niceties list. Later: PXC info overlay, PxcViewerActivity, unlock-banner 1-bit reuse, "Until Death" screen, skull boot logo.
4. First flashable test build once #9 + #10 land (bundle with themes + PXC).
