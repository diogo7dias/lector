# Witch(hunt) Reader Feature Study & Implementation Handoff

> **What this is.** A study of [`jpirnay/witchhunt-reader`](https://github.com/jpirnay/witchhunt-reader)
> (another crosspoint-reader fork) to decide which of its headline features are worth porting into
> Lector, plus a ground-truth map of what our tree **already has** and an implementation guide for the
> genuine gaps.
>
> **How to use it.** This was researched in an environment that **cannot build or flash** (see
> [Environment constraints](#environment-constraints)). Run the actual implementation from a machine
> that can `pio run` and flash the device (e.g. your SSH box). Treat each work item's *"Validate on
> device"* steps as mandatory — none of this has been compiled or run.
>
> **Read this first.** My original chat-based study compared witchhunt's feature *list* against a
> shallow grep of our tree and got several things **wrong**: it claimed features were missing that are
> in fact already implemented in the current tree. This document is the corrected version, built from
> reading the actual code. Trust this over the earlier chat summary.

---

## TL;DR — corrected verdicts

The six features I marked ✅ in the chat study, re-checked against the real tree:

| # | Feature | Chat verdict | **Ground truth** | Action |
|---|---------|--------------|------------------|--------|
| 9a | Strikethrough / superscript / subscript | "✅ build it" | **Already fully implemented** | **Skip** |
| 11 | Background pre-render of next section | "✅ build it (hard)" | **Already implemented** (synchronous, 1 chapter ahead) | **Skip / optional polish** |
| 2 | CSS font-size scaling (headings) | "✅ build it" | **Partial** — headings styled bold+centered, but not size-scaled | **Real gap — finish it** |
| 6 | Book information screen | "✅ build it" | **Missing** — and description isn't even parsed yet | **Real gap — 2 steps** |
| 5 | SD-backed file index (large folders) | "✅ build it" | **Missing** — full listing held in RAM | **Real gap — value depends on library size** |
| 1 | Lazy first-open pagination | "✅ build it (hard)" | **Missing** — whole section laid out eagerly | **Real gap — narrow benefit, high risk** |

**Bottom line:** two of the six are already done, one is half-done, and only three are genuine
greenfield work. The tree is **far ahead of "crosspoint 1.4.1"** — it has already absorbed most of
witchhunt's rendering work. Prioritise **#2** and **#6**; treat **#5** and **#1** as conditional.

---

## Environment constraints

Discovered while setting up to implement; they shape *where* the work should happen.

- **No local firmware build in the Anthropic web/remote env.** `pio` installs fine (Core 6.1.19), but
  the first `pio run` fails: the ESP32 platform download
  (`pioarduino/platform-espressif32 55.03.37`) returns **HTTP 403 from the agent proxy**. So the
  toolchain never lands and nothing compiles here. **Build and flash on your own machine.**
- **CI is the remote compile gate.** `.github/workflows/ci.yml` runs three jobs on push/PR:
  `clang-format` (LLVM **21**), `cppcheck`, and a PlatformIO build. Pushing a branch gets you a real
  compile — but it's a slow loop, so prefer building locally on your box.
- **clang-format version skew.** CI uses clang-format **21**; a typical dev box has 18. Run
  `bin/clang-format-fix` before pushing (the CI `clang-format` job does `git diff --exit-code`).
- **`docs/file-formats.md` is stale.** It documents `section.bin` as **v25**; the code is actually at
  **v30** (`lib/Epub/Epub/Section.cpp:18`). Don't trust the doc's version numbers — read the constants.

---

## Already done — do not rebuild (evidence)

### #9a — Strikethrough, superscript, subscript ✅ present

The CSS model and parser already handle all three:

- `CssStyle.h:56` — `enum CssTextDecoration { None, Underline, LineThrough }` (bit flags).
- `CssStyle.h:75` — `enum CssVerticalAlign { Baseline, Super, Sub }`.
- `ChapterHtmlSlimParser.cpp:37` — `LINETHROUGH_TAGS[] = {"del", "s", "strike"}`.
- `ChapterHtmlSlimParser.cpp:98,901` — line-through applied from both CSS `text-decoration:line-through`
  and the tags above; `:815` underline.
- `ChapterHtmlSlimParser.cpp:980-984` — `vertical-align: super` / `sub` mapped to `effectiveSup` /
  `effectiveSub`; consumed at `:161-219`.
- `CssParser.cpp:406-410, 741-912` — vertical-align/decoration parsed **and serialized** into the CSS
  cache.

Tables are also partially handled (`ChapterHtmlSlimParser.cpp:423,462` flatten cells into per-cell
paragraphs with a bold header prefix) — not a real grid, but present. Nothing to do here.

### #11 — Background pre-processing of sections ✅ present (synchronous)

`EpubReaderActivity::silentIndexNextChapterIfNeeded()` — `EpubReaderActivity.cpp:1202`:

- Fires when the reader is on the **penultimate page** of the current section (`:1210`).
- Loads or, if absent, **builds** the *next* spine item's section cache
  (`loadSectionFile` → else `createSectionFile`, `:1217-1232`) using the current render settings.
- Result: the next chapter is already indexed before you turn to it, so the "Indexing" popup rarely
  appears on chapter boundaries.

This runs on the main loop (no FreeRTOS task), which is the *safe* choice on a single-core chip with a
mutex-guarded SD bus. It already delivers witchhunt's "fewer Indexing messages" benefit.

**Optional polish (low priority, only if you see stalls):** move it to a low-priority background task
and/or prefetch depth 2. **Hard requirement if you do:** every SD access must go through the
`HalStorage`/`Storage` mutex (SdFat is not thread-safe — see `CLAUDE.md`). `createSectionFile` already
uses `HalFile`/`Storage` internally, so it's mutex-safe, but you'd need to guarantee the reader's own
draw path and the task never both touch the card in a way that starves the render task and triggers
ghosting. Given the synchronous version already works, the risk/reward here is poor. Recommend leaving
as-is.

---

## Work item #2 — CSS heading font-size scaling  (recommended, medium)

**What witchhunt does (ELI5).** Their v2.06 note: "the renderer now honors different font sizes from
the book's CSS." An `<h1>` renders visibly larger than body text, captions smaller, etc.

**Our current state.** Headings are *recognised and styled* but not *sized*:

- `ChapterHtmlSlimParser.cpp:32` — `HEADER_TAGS = {h1..h6}`.
- `ChapterHtmlSlimParser.cpp:849-859` — a header opens a new text block with
  `BlockStyle::fromCssStyle(...)`, centered, and bolded (`boldUntilDepth`, `:859`).
- **But** every text run is drawn with a single body `fontId`
  (`renderer.getLineHeight(fontId)`, `.getFontAscenderSize(fontId)` — e.g.
  `ChapterHtmlSlimParser.cpp:247,286,826`). There is no per-block larger font.
- The CSS infrastructure to drive this **already exists**: `CssLength`/`CssUnit` with
  `toPixels(emSize, containerWidth)` (`CssStyle.h:10-50`) and it's already used for image/margin/indent
  sizing (`ChapterHtmlSlimParser.cpp:559-627`). What's missing is applying a parsed `font-size` (and a
  sensible default per heading level) to **pick a larger font for the run**.

**Why it's a real gain.** Structured non-fiction/textbooks read much better with a visible heading
hierarchy. For plain novels it's near-invisible — but it's cheap and in-scope ("Typography &
Legibility").

**Implementation approach.**
1. **Fonts are discrete, not scalable.** Reader fonts exist at fixed sizes (Lector bakes Bookerly /
   Georgia / Verdana / Merriweather at **11–16**; see `src/main.cpp` global font objects and
   `src/fontIds.h`). "Scaling" therefore means **selecting a larger already-loaded font family/id** for
   the heading block, not rescaling glyphs. Confirm which larger sizes are actually compiled in for the
   active reader family before mapping.
2. Add a per-block *font-size class* to `BlockStyle` (`lib/Epub/Epub/blocks/BlockStyle.h`) — e.g. an
   enum/step `{ Smaller, Normal, H3, H2, H1 }` or a resolved target point-size. Populate it in
   `BlockStyle::fromCssStyle` from the CSS `font-size` (em/rem/%/pt via `CssLength::toPixels`) and/or a
   default table keyed on heading level (h1 biggest … h6 ≈ body).
3. In the layout path that reads `fontId` (start in `ChapterHtmlSlimParser` where blocks are built and
   lines measured — `:247,286,826`), map the block's font-size class to a concrete larger `fontId` for
   that block's line-height, word measurement, **and** render. Fall back to body font if no larger size
   is compiled in.
4. `TextBlock` stores per-word *style* (regular/bold/italic) but the **font id/size is a block-level
   property**, so you likely extend `BlockStyle` + the block serialize path, not the per-word arena.
   Simpler than a per-word change.

**Cache impact.** Layout geometry changes → **bump `SECTION_FILE_VERSION`** (`Section.cpp:18`, 30→31)
so old caches auto-invalidate. If you also persist a new field in the CSS cache, mind
`CssParser.cpp` serialize/deserialize + its own version.

**Risk.** Medium. Contained to layout; no concurrency. Main pitfall: a book whose CSS sets huge
heading sizes could overflow a line or a page — clamp the max selectable font id.

**Validate on device.**
- Delete `.crosspoint/` on the SD card (force re-index) after the version bump.
- Open a book with clear `<h1>/<h2>` (most non-fiction). Confirm headings render larger, still wrap,
  and don't clip at page top/bottom.
- Check a heading that lands at a page break isn't half-cut.
- `ESP.getFreeHeap()` before/after opening a heading-heavy chapter — no new leak.

---

## Work item #6 — Book information screen  (recommended, low risk, 2 steps)

**What it is (ELI5).** A screen showing cover + title/author/metadata + a scrollable/paged
description/synopsis. Reached from the reader menu or the file browser.

**Our current state.**
- No such activity exists (`src/activities/reader/`, `src/activities/home/` — none match info/detail/
  about).
- **The book description is not parsed today.** A grep for `description`/`dc:description` across
  `lib/Epub/` finds nothing. Title/author *are* parsed (used on the home/list screens), but the OPF
  `<dc:description>` is dropped. **So this is two steps, not one.**

**Step A — capture the description (metadata plumbing).**
- Parse `<dc:description>` in the OPF parser (`lib/Epub/Epub/parsers/ContentOpfParser.cpp`) alongside
  the existing title/author extraction.
- Persist it in the book metadata cache (`book.bin`). Find the writer/reader (search
  `lib/Epub/Epub/BookMetadataCache*` / wherever `book.bin` v7 is written) and add a length-prefixed
  string field. **Bump the `book.bin` version** (currently **7**) so caches regenerate. Keep the field
  optional/empty-safe — many EPUBs have no description.
- Beware: descriptions can contain HTML. Strip tags to plain text at parse time (there's already HTML/
  entity handling in the parser utils) rather than storing markup.

**Step B — the screen (UI).**
- Add a `BookInfoActivity` following the standard lifecycle
  (`src/activities/Activity.h`; e.g. mirror `EpubReaderMenuActivity` or a simple existing full-screen
  activity). Allocate buffers in `onEnter`, free in `onExit`.
- Render cover (reuse the existing cover pipeline — covers are decoded to BMP via
  `JpegToBmpConverter`/`PngToBmpConverter` and cached under the book's `.crosspoint/` dir; the home
  screen already draws them), title, author, then the description as a **paged** text block (reuse the
  reader's line-wrapping or the simple text pager; the description is short, so no section-cache
  machinery needed).
- Launch it from `EpubReaderMenuActivity` (add a menu row) and/or a long-press/menu entry in the file
  browser. Use `tr()` for every label — add a `STR_BOOK_INFO` (and any others) to
  `lib/I18n/translations/english.yaml`, then run
  `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/` (generated files are gitignored — commit
  only the YAML).

**Cache impact.** `book.bin` version bump (Step A). No section-cache change.

**Risk.** Low. Self-contained; near-zero steady-state RAM (cover buffer already exists; description is
a small string). One new activity + one parser field.

**Validate on device.**
- Delete `.crosspoint/` (or at least the affected `book.bin`) so the new field is written.
- Open a book *with* a `<dc:description>` and one *without* — confirm graceful empty state.
- Confirm cover draws in all 4 orientations and the description pages correctly.
- Heap check on enter/exit (no leak; buffers freed in `onExit`).

---

## Work item #5 — SD-backed file index for large folders  (conditional, medium)

**What witchhunt does (ELI5).** With ~380 KB RAM you can't hold a 2000-entry directory listing in
memory. They keep the index on the SD card and page it in with a bounded RAM budget, so huge libraries
browse without OOM.

**Our current state.** The whole listing lives in RAM:
- `FileBrowserActivity.h:41` — `std::vector<std::string> files;` (every filename, full strings).
- `FileBrowserActivity.h:49` — `std::vector<size_t> filteredIndexes;` (search/filter view).
- No windowing, no SD-backed index.

**Is the gain real?** Only for users with **very large single folders** (roughly 1000+ books in one
directory). For a few dozen books it's pure overhead. **Gate this on whether real users hit it.** If
your library is modest, skip — it fails the SCOPE.md "improves the average user's reading" test.

**Cheaper alternatives to consider first (recommended before a full rewrite):**
- **Cap + lazy strings.** Most of the cost is the `std::string` filenames. Reserve up front
  (`files.reserve(n)` — the codebase mandates reserve-before-push) and/or store offsets into one flat
  char arena (same trick `TextBlock` already uses) instead of N heap strings. This alone removes most
  of the fragmentation without an on-SD index.
- **Windowed load.** Read the directory in pages of, say, 200 entries around the cursor; only
  materialise the visible window + a margin.

**Full implementation (only if the above isn't enough).**
- Build a one-time index file under `.crosspoint/` (e.g. sorted offsets/names) via `HalStorage`, then
  seek+read the visible window on scroll. Keep a fixed-size RAM window (e.g. current page ± margin).
- All I/O through `Storage`/`HalFile` (mutex). Invalidate/rebuild the index when the folder's mtime or
  entry count changes.

**Cache impact.** New optional index file; no existing format change. Make a corrupt/short index
rebuild rather than fault (matches the codebase's "detect corrupt cache, rebuild" philosophy).

**Risk.** Medium; touches a high-traffic UI path. Easy to regress scroll performance.

**Validate on device.** Create a folder with 1500+ files. Confirm: browse without OOM reboot, scrolling
stays responsive, search/filter still works, heap stays >50 KB. Compare against the cheap-alternative
approach before committing to the full index.

---

## Work item #1 — Lazy first-open pagination  (conditional, high risk, narrow benefit)

**What witchhunt does (ELI5).** For a book that is one giant spine (a single chapter with hundreds of
pages — common in cheap EPUBs), the base engine lays out the *entire* chapter before showing page 1.
Their v2.07 note: first open dropped from ~1 min to a few seconds by laying out just enough to draw the
current page and deferring the rest.

**Our current state.** Eager, whole-section pagination:
- `Section::createSectionFile` → `visitor.parseAndBuildPages()` (`Section.cpp:266`) parses the entire
  section HTML and emits **every** page into a lookup table (`onPageComplete`, `Section.cpp:262`)
  before returning.
- The "Indexing" popup (`popupFn`) covers this blocking work.
- For normal multi-chapter EPUBs this is fine (sections are small, and `silentIndexNextChapterIfNeeded`
  hides chapter-turn cost). **The pain is only the first open of a pathological single-spine book, and
  only once** — it's cached afterward.

**Is the gain real?** Yes but **narrow**: one-time, first-open, single-spine books only. Weigh that
against the risk of restructuring the core pagination/serialisation path — the highest-risk change in
this document, on firmware whose first rule is "stability is non-negotiable," which you can only
hardware-test manually.

**If you do it — approach.**
- The section cache stores a page LUT + serialized pages. To show page 1 before the whole section is
  laid out, you'd paginate incrementally: lay out and persist page N, render it, then continue building
  the rest (ideally on the existing `silentIndex...` idle hook) and append to the LUT/cache.
- This means the section cache must tolerate being **partially built** (a page count that grows) and be
  resumable — a meaningful change to `Section`'s create/load/serialize contract. **Bump
  `SECTION_FILE_VERSION`.**
- Reuse the mutex-safe `HalStorage` path throughout; do not introduce a second thread for this — extend
  the existing single-threaded incremental hook instead.

**Risk.** High. Partial-cache states, resume-after-power-loss, progress mapping (KOReaderSync's
`ProgressMapper` and anchor/paragraph LUTs assume a complete section) all get more complex.

**Recommendation.** Do this **last, in its own PR**, only after #2/#6 are landed and validated, and only
if single-spine books are a real part of your library. Otherwise the cost/benefit doesn't clear the bar.

**Validate on device.** A known single-spine book (one `<spine>` item, hundreds of pages): time to
first page on a cold cache; then verify total page count matches the old eager result exactly, page
navigation is correct end-to-end, KOReader progress still maps, and a mid-index power-cut leaves a cache
that rebuilds cleanly.

---

## Cross-cutting rules (apply to every item)

- **Cache versions are your safety net.** Any change to laid-out geometry → bump `SECTION_FILE_VERSION`
  (`Section.cpp:18`). Any change to book metadata → bump the `book.bin` version. Mismatch
  auto-invalidates and regenerates the cache. Update `docs/file-formats.md` (already stale) when you do.
- **All SD I/O through `HalStorage`/`Storage` + `HalFile`.** Never touch SdFat/`FsFile` directly — the
  mutex is what keeps the single SPI bus safe (`CLAUDE.md`).
- **The `TextBlock` arena already exists** (per-word text/xpos/style/focus in one `makeUniqueNoThrow`
  allocation, `TextBlock.h`). Don't "add" it — witchhunt's "flatten into an arena" optimisation is
  already here. Font-size (#2) is block-level, so you extend `BlockStyle`, not the per-word arena.
- **KOReaderSync already exists and is substantial** (`lib/KOReaderSync/` — client, `ProgressMapper`
  902 lines, `ChapterXPathResolver`, credential store). Any pagination change (#1) must keep progress
  mapping correct.
- **No bare `new`** — `makeUniqueNoThrow<T>()` / `new (std::nothrow)` only (`CLAUDE.md`). **Reserve
  before `push_back`.** **All UI text via `tr()`**; edit `english.yaml` + regenerate i18n; commit YAML
  only.
- **Format before pushing:** `bin/clang-format-fix` (CI uses clang-format 21).

## Suggested order & PR strategy

1. **#6 Book info screen** — highest value-to-risk; self-contained; good first PR. (2 commits: metadata
   capture, then the screen.)
2. **#2 Heading font-size scaling** — real reading improvement, contained to layout. Own PR (touches the
   section cache version).
3. **#5 SD file index** — only if you actually hit large folders; try the cheap arena/windowing fix
   first. Own PR.
4. **#1 Lazy first-open** — last, own PR, only if single-spine books matter to you.

Keep each in its own PR (the codebase values small, single-concern changes — see the `refactor-for-review`
skill). #9a and #11 need no work.
