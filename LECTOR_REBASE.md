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

- [ ] **PXC sleep wallpaper** (CURRENT PRIORITY, before 9/10). CrossPoint takes only
      `.bmp`; add `.pxc` acceptance via `renderPxcSleepScreen` (on-demand decode, no staging).
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

## Current task — PXC sleep wallpaper

- CrossPoint sleep code: `src/activities/boot_sleep/SleepActivity.{h,cpp}` — accepts `.bmp`
  (`/sleep.bmp` + `.bmp` files via `FsHelpers::hasBmpExtension`), renders via `renderBitmapSleepScreen`.
- Lector core to port: `src/activities/boot_sleep/PxcSleepRenderer.{h,cpp}`
  (`renderPxcSleepScreen`, streams row-by-row) + `PxcOverlayTiming.h`. Uses grayscale plane
  rendering that freeink-sdk provides. Also `PxcViewerActivity` (view a `.pxc`) if wanted.
- Plan: add `.pxc` as an accepted wallpaper extension in `SleepActivity`; when the chosen
  wallpaper is `.pxc`, call `renderPxcSleepScreen` instead of the BMP path. On-demand decode
  only — do NOT bring `SleepWallpaperStage`/`SleepWallpaperIndexStore`.
- `.pxc` files are produced by the Wallpaper Converter site (diogo7dias.github.io/lector-wallpaper-converter).

## Next steps

1. Port PXC sleep wallpaper (current task).
2. Port #9 per-book reader settings (on CrossPoint indexing).
3. Port #10 paragraph numbers (3 states) on top of #9.
4. Continue down the niceties list.
5. First flashable test build once PXC + 9 + 10 land.
