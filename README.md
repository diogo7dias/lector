# Lector

A personal fork of [CrossPoint](https://github.com/crosspoint-reader) e-reader firmware for the ESP32C3-based Xteink X4/X3, branched from CrossPoint **1.4.1**.

> 🖼️ **[Wallpaper Converter](https://diogo7dias.github.io/lector-wallpaper-converter/)** — turn any image into an X3/X4 sleep wallpaper (`.pxc` or `.bmp`), in your browser. ([source](https://github.com/diogo7dias/lector-wallpaper-converter))

Everything CrossPoint does still works here — see the [upstream project](https://github.com/crosspoint-reader) for the full feature set and docs. This file only lists what Lector changes on top of it.

## Differences from CrossPoint main

**Fonts & typography**
- Flash-baked reader font set: Bookerly, Georgia, Verdana, Merriweather (sizes 11–16).
- Cozette UI font in place of Ubuntu.
- Portuguese (pt) hyphenation.
- Paperback Look: heavier in-book text, toggled from the in-book menu.
- Text anti-aliasing disabled (removes the fading-text refresh, notably on X3).
- First-line indent: Book mode or a custom 0–100% of the column.
- Word Spacing (−30%…+300%) and Paragraph Spacing (0–150%) sliders.
- Percentage-based line spacing; list-picker UI for enum settings.
- "Focus Reading" renamed to "Bionic Reading".

**Home screen**
- List home layout (recent-books list) alongside the single-cover home, via a `homeLayout` setting.
- `[NN%]` reading-progress badge on list rows; "Opening…" banner with full-refresh on entry.
- Back removes the selected recent book; Recent Books moved into the file browser.
- Pages-read counter and centered clock in the header (X3); DX34-style Pages button with reset.

**Status bar (v2)**
- Per-item, 6-anchor placeable status bar (title / page / battery / clock / progress bars), migrated from the old fixed layout.
- Fall-down reflow for greedy titles; multi-line title wrap with truncation off by default.
- TXT reader wired onto the same v2 bar; enlarged reader status text.

**Reader menu & selection**
- In-book menu tidy-up: hidden Toggle Bookmark / Auto Turn, added "by author" and chapter-name header with title wrap.
- Grab Quote: select a passage and save it to `<book>_QUOTES.txt`.

**Margins**
- Uniform-margins toggle plus independent top/bottom margins; top/bottom are additive over the status-bar band (max 100px).

**Sleep & boot**
- "Until Death" sleep screen; unlock shows the current wallpaper.
- Random skull-crest boot logo (5 images) with a 4-block segmented boot loader.

**UI polish**
- Banner-style (full-width black bar) popups with bolder text.
- Throttled, wrapped font-download progress screen.

**Input**
- DX34 snappiness port: keep-alive on held buttons and tighter loop delays (10ms floor to protect the render task from ghosting).

**Misc**
- Open-random-book-on-boot option.
- CI runs on `main` (the fork's default branch).

## TODO (to implement later)

Ports still wanted from the DX34 fork:
- **Reading themes** — in-book theme presets + revert.
- **Poems view** — dedicated poems / quotes browser.
- **Storage cleanup activity** — dedicated cleanup, separate from clear-cache.
- **More status bar items** — e.g. quote count, pages-left.
