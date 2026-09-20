# CrossPoint versus lector: reading-path investigation

## 1. Verdict

**Cannot tell without hardware whether upstream is measurably faster from accepted input to finished pixels; upstream does less work on several traced paths, but the host layout comparison does not show an upstream advantage.**

Investigation only, 2026-09-20. No device was available: no panel timings, SD throughput measurements, flash, push, tag, or release were performed. Source comments containing historical device timings are not measurements from this investigation. The owner's observation remains plausible, not established for these revisions and settings.

Pinned sources and citation convention:

- **O** = lector `8440647dc6c2eb07e06244d1bbd9a4151b0a1738`, starting HEAD of `investigate/crosspoint-perf-gap`.
- **U** = upstream/develop `8c84ef3268fd56147ae1625e04f773f0f50b7720`.
- **OS** = lector's FreeInk SDK gitlink `a63778180333eb5066e0edc1c488826619f35a02`.
- **US** = upstream's FreeInk SDK gitlink `ffcbb1c7a38c357c4a37deb68c0c6dceadc50768`.
- `O:path:line` and `U:path:line` refer to files at those commits, not a moving branch. SDK citations use paths relative to `freeink-sdk/`. Read with `git show <SHA>:<path> | nl -ba`. Upstream was read this way into `/tmp/perfgap-evidence/upstream`, never checked out over the worktree. SDK objects were read from the existing SDK repository at `/home/claude/projects/active/lector/freeink-sdk`.

Target: ESP32-C3, approximately 380 KB RAM, **no PSRAM**. X4/SSD1677 and X3/UC8253 need separate conclusions. X4 Pro/S3, Paper Mono combined grayscale, and PSRAM-dependent paths are excluded. The owner's actual panel, installed binary, font, anti-aliasing, cleanup interval, and cache state were not supplied; source defaults are not proof of device settings.

## 2. Evidence table

**MEASURED** means a host execution reported below; **TRACED** means executable source establishes the work/count, not its elapsed device time; **UNVERIFIED** means the proposed timing effect was not established.

| Candidate | Comparison and finding | Evidence / classification |
|---|---|---|
| SD SPI transfer build flag | U defines `USE_SPI_ARRAY_TRANSFER=1`; O does not. For the inspected SdFat SPI driver, one 512-byte payload uses 512 byte-transfer calls at value 0 versus one buffer-transfer call at value 1. Both still transfer 512 payload bytes; FAT/cache misses determine how many sectors a page actually needs. This affects section/page, metadata, image and SD-font reads. **Do not interpret U's “~3x” comment as our measurement.** | **TRACED**: U:`platformio.ini:49–56`; O:`platformio.ini:33–75`; dependency trace below. U commit `e33e3cf39d66a35905782866b3cf04a7223b2e31`. Actual speedup **UNVERIFIED**. |
| Optimization/toolchain/logging | Neither committed ini explicitly changes `-O*` or `board_build.f_cpu`. Same C3 board selection, but platform versions differ: O `55.03.37`, U `55.03.311`. Both development profiles use `LOG_LEVEL=2`, releases `LOG_LEVEL=1` with serial enabled. O development additionally forces performance logging. There is no explicit O0-versus-O2 explanation in the repository flags. | **TRACED**: O:`platformio.ini:17–19,33–89,223–252`; U:`platformio.ini:10–12,33–89,209–228`. Local C3 builder uses `-Os`, board definition 160 MHz; effective upstream compiler/core flags and the owner's binary are **UNVERIFIED**, not rebuilt here. |
| CPU power management | O configures DFS with 80 MHz minimum and boot frequency maximum; U requests 10 MHz idle on no-PSRAM C3. **Both hold a full-speed lock over rendering**, and restore full speed on input. O's tickless idle does not establish that it renders at 80 MHz. | **TRACED**: O:`lib/hal/HalPowerManager.cpp:226–227,262–267,317–319,560–579`, `src/activities/ActivityManager.cpp:148–187`; U:`lib/hal/HalPowerManager.h:27–33`, `.cpp:29,43–63,162–181`, `src/activities/ActivityManager.cpp:53–64`. Wake/scheduling cost **UNVERIFIED**. |
| SPI clock rates | SD default is 40 MHz in both SDKs. X4 and X3 display profiles use **20 MHz in O versus 10 MHz in U**. That is opposite to an upstream wire-speed advantage. It does not establish overall page latency. | **TRACED**: OS:`libs/hardware/SDCardManager/src/SDCardManager.cpp:102`; US: same file `:128`; OS:`libs/hardware/BoardConfig/include/BoardConfig.h:783–795,825–833`; US: same file `:846–867,889–901`. |
| C3 WiFi-buffer comment | O's lines 157–164 describe a **reverted** reduction. Current C3 block sets only `CONFIG_ESP_WIFI_CSI_ENABLED=n`; the reduced RX/TX settings are absent. Both profiles disable WiFi IRAM optimizations. No evidence that those reverted buffer cuts explain offline cached reading. | **TRACED**: O:`platformio.ini:111–117,156–173`; U:`platformio.ini:158–164,189–204`. WiFi-active interference needs its own measurement. |
| Ordinary text refresh | With AA off, each ordinary successful render submits one base refresh in both. Reader cadence requests HALF initially and every N pages, FAST otherwise. O alone may promote FAST to HALF/FULL according to its global count/ink budget; promotion replaces a call rather than appending another reader refresh. | **TRACED**, plus **MEASURED host policy counts** below: O:`src/activities/reader/ReaderUtils.h:136–147`, `lib/hal/HalDisplay.cpp:74–104,165–211`, `lib/hal/DisplayRefreshPolicy.cpp:14–63`; U:`src/activities/reader/ReaderUtils.h:162–173`, `lib/hal/HalDisplay.cpp:62–78`. |
| X4 fast waveform | O's `fastPageTurns=1` default enables the opted-in incremental fast sequence; seven eligible passes can use Turbo, every eighth requests Standard. U's stock X4 configuration uses the standard `0xFC` fast sequence. O has a plausible advantage on eligible FAST turns, counterbalanced by more cleanup. Dark-background O disables Turbo in the driver. | **TRACED**: O:`src/CrossPointSettings.h:639–646`, `src/main.cpp:1023`, `src/platform/LectorSsd1677Config.cpp:34–47`, `lib/hal/DisplayRefreshPolicy.h:86–112`; OS:`libs/display/FreeInkDisplay/src/driver/Ssd1677Driver.cpp:298–303,379`; US: same file `:59–61,280–286`. Device gain **UNVERIFIED**. |
| Image-page refresh count | For an image with a valid bounding box, O submits two FAST bases and optionally a preceding HALF; U submits one FAST-or-HALF base. Both add one placeholder FAST when decoding is needed, and a grayscale submission on success. O also re-renders page content between its two FAST bases. | **TRACED**: O:`src/activities/reader/EpubReaderActivity.cpp:3189–3253,3326,3381`; U: same file `:1569–1614,1660–1674,1708–1723`. Count table below. U simplification: `d3b3b5669595c8fd1417d84ee0558e5aa5df2186`. |
| X3 driver waits | O has two `waitPanelIdle()` calls in the ordinary BW completion path: after the refresh wait and after the old-plane resync. Each demands 8 ms continuously high BUSY (up to a 2000 ms drain ceiling). U has neither. These are actual extra waits, even though a nearby comment calls them a “no-op” when finished. Both retain the non-FAST `delay(200)`. | **TRACED**: OS:`libs/display/FreeInkDisplay/src/driver/Uc8253X3Driver.cpp:91–108,282–302,325–333`, `.h:91–100`; US: `.cpp:262–279,302–310`. Additional conditioning triggers also call the O stability wait. Hardware overhead and necessity **UNVERIFIED** here. |
| Frame scoring | O calls `inkMetrics.update()` **before** checking whether the request was FAST, so HALF/FULL buffer submissions scan too. X4: 48,000 bytes; X3: 52,272 bytes, with an ink lookup and hash update per byte. U's HAL delegates without this scan. This is per submission, not every `loop()`. | **TRACED**: O:`lib/hal/HalDisplay.cpp:131–143,165–176,181–190`; `lib/hal/FrameInkMetrics.cpp:42–77`; U:`lib/hal/HalDisplay.cpp:62–75`. Comments claiming the HALF scan is skipped do not match execution order. |
| Valid page/section caches | Both load matching section headers once on section creation and deserialize individual cached pages without layout. Normal valid `loadPageAt`: **one open, five seek calls, four POD reads before page deserialization**, then close via RAII. Counts are API calls, not physical SD transactions. Page content determines subsequent reads/allocations. | **TRACED**: O:`lib/Epub/Epub/Section.cpp:236–312,835–883`, reader `:2513–2539`; U: Section `:146–228,753–801`, reader `:1166–1192`. No evidence of routine re-layout on a valid O hit. |
| Cache misses / partial builds | Both keep unzipped HTML and partial page caches, rebuild from chapter start when extending a reopened partial, defer extension until within 15 pages, build two pages per background tick with a five-page lookahead, and request eight-page foreground chunks. O yields when a foreground chunk still cannot serve the waiting page; U keeps pumping under the render lock. | **TRACED**: O: Section `:379–439,528–558`; U: Section `:292–348,454–480`; O reader `:776–833,2852–2902`, `.h:220–221,256–263`; U reader `:377–404,1314–1343`, `.h:115–123`. O's bail improves input availability, not proven completion time. |
| Cache compatibility | Section format **61 O / 46 U**, metadata **12 O / 10 U**. Each rejects the other's versions. O has more render-spec fields, but validates them on section load, not on every page. Switching firmware or settings can force a cold rebuild; warm-versus-cold comparisons are invalid. | **TRACED**: O: Section `:112,246–277`, `lib/Epub/Epub/BookMetadataCache.cpp:17,512–524`; U: Section `:54,156–194`, BookMetadataCache `:14,460–471`. No cache inventory from the device: whether this explains the report is **UNVERIFIED**. |
| Metadata cache cost | Both retain cumulative spine sizes in RAM, so progress percentage is not an O(spine count) disk walk per page. O's initial metadata load uses a 512-byte buffered scan skipping href strings; U materializes a SpineEntry per spine. O avoids that string construction/read work on book open. | **TRACED**: O:`lib/Epub/Epub/BookMetadataCache.cpp:537–569`; U: same file `:484–505`. Extra 512-byte transient buffer in O; no PSRAM assumed. |
| Idle prewarm | U has an equivalent in `loop()`: same 400 ms debounce, render/build and heap gates, one neighbour-page deserialize and one font scan per attempted position. U always chooses next page; O chooses previous after same-spine backward motion. Neither prewarms another chapter. | **TRACED**: O reader `:740–773`, `src/activities/reader/IdlePrewarmNeighbour.h:9–18`; U reader `:352–374`. No systematic extra O prewarm; O is better directed for backpaging. Both can occupy the lock before input handling if a prewarm has started. |
| Status bar / adornments | Both scan and paint the status bar on a normal text render. O always fetches chapter metadata even if the selected bar does not need it; U only fetches chapter metadata for chapter-title mode. O optionally opens/scans the paragraph LUT twice, once per status-bar call. Paragraph numbering adds a page-element walk/drawing; quotes add work only with saved anchors. **None is recomputed by an idle reader loop.** | **TRACED**: O reader `:3150–3158,3200–3212,3448–3482,2131–2173,2242–2244`; U reader `:1530–1537,1576–1577,1765–1795`. Details below. |
| Progress persistence | U saves after every changed rendered page (also a page-count change); O observes each render and saves after ten position changes or five minutes when next observed, with explicit exit saves. Ordinary `loop()` does not continually save progress. For ten turns after initialization within five minutes: **one batch save O versus ten U**, excluding explicit saves/failures. | **TRACED**: O reader `:2989–2990,3073–3098,3122–3128`, `src/activities/reader/ReaderProgressSaveDebouncer.h:12–13,25–50`; U reader `:1417–1423`. This favors O for SD writes; saves follow rendering, so it is not automatically a first-pixel advantage. |
| Grayscale scratch allocations | In the strip-fallback tier, U allocates/frees one `widthBytes * 80` scratch array per page; O retains/reuses it until release or a size change. X4 scratch is 8,000 bytes; X3 is 7,920. Both may allocate whole-plane buffers when heap/overlap allow. | **TRACED**: O reader `:3101–3119,3267–3269,3343`; U reader `:1617–1621,1673–1674`. One fewer scratch allocation/free pair on subsequent O fallback pages, at the cost of retained RAM. |
| Loop polling, diagnostics | O adds input-binding bookkeeping, PM-lock checks, constant-size timing counters and a once-per-minute low-battery check. Both memory logs are guarded by serial and ten seconds. U, not O, has a tilt-sensor update; it returns immediately if unavailable. O performance CSV writes are batched per refresh, not per loop; release default is off, development forces on. Diagnostic persistence is event-driven. | **TRACED**: O:`src/main.cpp:970–983,1002–1061,1268–1323`, `src/PerfLogSink.cpp:31–46`, `lib/PerfLog/PerfLog.cpp:16,76–89,120–148`, `src/Diagnostics.cpp:302–370`; U:`src/main.cpp:608–615,760–792`, `lib/hal/HalTiltSensor.cpp:65–68`. Aggregate device cost **UNVERIFIED**. |
| Idle panel power-off hypothesis | O calls `display.powerOffPanel()` after two seconds. However the pinned X3 and SSD1677 drivers **do not override** `PanelDriver::powerOff()`, whose body is empty. The call cannot establish the tempting claim that every leisurely turn becomes HALF. The facade can still drain a pending async refresh. | **TRACED negative finding**: O:`src/main.cpp:1307`, `lib/hal/HalPowerManager.h:140`, `lib/hal/HalDisplay.cpp:259`; OS:`libs/display/FreeInkDisplay/src/FreeInkDisplay.cpp:972–976`, `driver/PanelDriver.h:95`, `driver/Uc8253X3Driver.h:47–86`, `driver/Ssd1677Driver.h:72–115`; U:`src/main.cpp:775–793` lacks this call. |
| Layout kernel | Same EPUB-derived paragraphs and identical stub font metrics: O median **1352.65 ms**, U **1422.32 ms**, each for 1,000 complete corpus passes. O is 4.9% lower by median; ranges overlap. This narrow host result does not demonstrate a meaningful device layout advantage for either tree. | **MEASURED**, methodology and raw samples below. Full Section + ZIP + SD-font + storage timing remains **UNVERIFIED**. |

### Build/dependency trace and its limits

The inspected local SdFat headers are under `/home/claude/projects/active/lector/.pio/libdeps/default/SdFat/src/`. `SdFatConfig.h:184–192` defaults array transfers to zero on ESP32; `SdCard/SdSpiCard/SpiDriver/SdSpiLibDriver.h:56–77` selects the byte loop or one in-place buffer transfer. Its write path `:83–96` uses a **512-byte stack scratch** for array transfers. Both SDK dependency manifests request `greiman/SdFat ^2.3.1` (`OS/US:libs/hardware/SDCardManager/library.json:11–13`), not an exact lock. The flag difference is certain; reproducing the owner's resolved dependency and stack budget is necessary before applying it. No such change was made.

The installed board JSON `/home/claude/.platformio/platforms/espressif32/boards/esp32-c3-devkitm-1.json:4` specifies `160000000L`; the installed C3 Arduino builder `/home/claude/.platformio/packages/framework-arduinoespressif32-libs/esp32c3/pioarduino-build.py:101` supplies `-Os`. These local files corroborate defaults, not both firmware binaries. This worktree had no `platformio.local.ini`. The platform-version difference remains an uncontrolled build variable; no claim that the binaries have identical machine code or core configuration is justified.

### Accepted input to panel completion

For an ordinary in-section cached turn:

1. **O** detects the page-button press in `src/activities/reader/ReaderUtils.h:122–128`; reader `:981–1021` calls `pageTurn()`, which changes position and requests rendering at `:2361–2403`. **U** selects press versus release/hold from the long-press setting (`ReaderUtils.h:51–69`); reader `:618–680` also has a **200 ms manual-turn guard** and one pending direction. U `pageTurn()` is `:1046–1084`. This input-policy difference can favor O, especially during fast flipping; it cannot explain U being universally faster. Start a hardware latency timer at the actually accepted action, and separately record physical press-to-action delay.
2. Both managers notify a render task and acquire a render mutex and power lock: O `src/activities/ActivityManager.cpp:148–187,336–340`; U `:53–64,197–201`. On C3 these tasks share one CPU. Background work can contend; they are not parallel CPU cores.
3. Both read a page from the section cache (counts in the table), scan text for font prewarm, then rasterize BW and draw the bar: O reader `:2942–2987,3150–3213`; U `:1376–1414,1530–1578`. No layout call occurs on this successful valid-page path.
4. `ReaderUtils::displayWithRefreshCycle()` chooses FAST/HALF. `GfxRenderer` forwards to the HAL, with blocking fallback for `fadingFix`: O `lib/GfxRenderer/GfxRenderer.cpp:1742–1770`; U `:1699–1724`. O's HAL scores/promotes/logs; U's delegates. The single-buffer SDK dispatches the framebuffer to its panel driver: OS `libs/display/FreeInkDisplay/src/FreeInkDisplay.cpp:578–610`; US `:590–625`.
5. The driver transfers planes and activates a waveform. SSD1677 chooses FULL `0xF7`, HALF `0xD7`, standard FAST `0xFC`, or O's eligible incremental fast path (SDK citations above). Blocking display waits for completion; async display waits later before plane uploads. With AA, both re-render grayscale bands and submit grayscale, then restore the BW baseline. O reader `:3266–3395`; U `:1617–1723`.
6. Progress saving happens after `renderContents()` returns, not before the base paint. A base-image preview is not equivalent to final grayscale pixels. Controller BUSY completion is not necessarily the instant the page looks settled; an optical measurement is needed too.

`FAST` here is a differential waveform, **not proof of a small rectangular transfer**. `HALF` is a cleanup mode, not half the screen. Refresh API counts must not be confused with physical waveform counts. U also has a one-shot renderer promotion (`lib/GfxRenderer/GfxRenderer.cpp:1693–1707`), set by the frontlight panel (`src/activities/util/FrontlightPanelActivity.cpp:157`); this is not an ordinary C3 page-turn cleanup cadence and is excluded from the no-overlay counts.

### Refresh counts, including controller work

Assume successful allocation/decoding, no popup/overlay, no initial boot resync, identical settings, and no HAL promotion unless stated. Image rows assume a valid image bounding box and the C3 overlay-grayscale path.

| Page | O reader submissions | U reader submissions |
|---|---|---|
| Text, AA off, non-cleanup turn | 1 FAST | 1 FAST |
| Text, AA off, scheduled cleanup | 1 HALF | 1 HALF |
| Text, AA on | 1 base + 1 grayscale; base can overlap grayscale computation | 1 base + 1 grayscale; base can overlap grayscale computation |
| Cached image, no cleanup pending | 2 FAST + 1 grayscale = **3** | 1 FAST + 1 grayscale = **2** |
| Cached image, cleanup pending | 1 HALF + 2 FAST + 1 grayscale = **4** | 1 HALF + 1 grayscale = **2** |
| Image needs decoding | Add 1 placeholder FAST to the applicable row | Add 1 placeholder FAST to the applicable row |

The page after an image is forced to cleanup in both (O counter becomes 0; U becomes 1; both satisfy `<=1`). Defaults differ: O `textAntiAliasing=0` (`src/CrossPointSettings.h:435`), U `=1` (`:243`). O per-book preferences can override it. Comparing defaults without recording effective settings would mix different work and visual quality.

On a warm X3, both HALs explicitly request a resync for a caller-requested HALF. The driver then runs the full-sync waveform, one requested conditioning waveform, and a no-op FAST settle: **three refresh triggers for that one HALF call**. A plain FULL with no forced/initial conditioning runs full-sync plus the FAST settle: two. A warm FAST or plain unforced HALF uses one. Sources: O HAL `:122–124,172–176`; U HAL `:62–75`; OS X3 driver `:210–254,304–347`; US X3 driver `:213–244,281–317`. O's promoted HALF deliberately avoids the forced resync. Grayscale has its own conditioning paths, so count its triggers separately; for example U explicitly preconditions scheduled AA cleanup at reader `:1600–1605`, while O calls the helper only when `!overlapRefresh` at `:3246–3252`. Both pinned X3 drivers advertise async support (`OS/US:.../driver/Uc8253X3Driver.h:62/63`), despite stale “blocking X3” comments in the reader. This is another reason to count controller commands on hardware instead of assuming one HAL call equals one waveform.

A host simulation used the **actual O `DisplayRefreshPolicy.cpp`**, the shared reader cadence (initial counter 0, N=15), 60 text pages, fresh policy state, no overlays or resyncs. `score=300` is a synthetic input, **not a measured page's ink**; AA adds the actual policy's `noteExternalFastPass()` after each base.

| Synthetic workload, 60 pages | O base FAST / HALF / FULL | U base FAST / HALF / FULL | Gray submissions each |
|---|---:|---:|---:|
| AA off, score 0, Turbo off | 52 / 7 / 1 | 56 / 4 / 0 | 0 |
| AA off, score 300, Turbo on | 48 / 12 / 0 | 56 / 4 / 0 | 0 |
| AA on, score 300, Turbo off | 44 / 14 / 2 | 56 / 4 / 0 | 60 |

These are **MEASURED host policy outputs**, not panel measurements. The cap is global: 12 consecutive FASTs cause a subsequent FAST request to become HALF; 48 FASTs since FULL cause a subsequent FAST request to become FULL. Ink debt can clean earlier; grayscale contributes to the counters. Consequently “one FULL every 48 page turns” is not an exact claim about actual reading. Sources: O `lib/hal/DisplayRefreshPolicy.h:9–16,35–47,100–124`, `.cpp:14–63`, `lib/hal/HalDisplay.cpp:328–333`. The synthetic U counts use its cadence with no additional HAL policy.

### Cache, status bar, and per-loop findings

A finalized section hit does not parse XHTML in either tree. On a miss, both reuse `/html/<spine>.html` if present, otherwise inflate it and start layout; partial caches serve their existing pages while a build catches up. O's additional header checks do not establish repeated invalidation in practice. Metadata loading has a one-time per-book O(spine count) scan in **both**, not a new lector per-turn loop. Section paths are constructed in O `lib/Epub/Epub/Section.cpp:139–143`, U `:81–85`; cache acceptance follows the version/spec checks cited above.

The real extra status-bar I/O is more specific than “recompute every loop.” Each O `renderStatusBar()` unconditionally calls `getTocIndexForSpineIndex()`, then `getTocItem()` when valid. Those route to disk-backed entries (O `lib/Epub/Epub.cpp:916–927,959`; `BookMetadataCache.cpp:572–615`). With a valid TOC and ordinary scan+paint, that is **two spine-entry plus two TOC-entry fetches**, eight entry-lookup seeks, before payload reads. U makes these title fetches only in chapter-title mode (`U reader:1781–1791`; `U Epub.cpp:945–956,988`; `U BookMetadataCache.cpp:508–548`). In that mode it also pays them, so this is not an unconditional O-versus-U delta.

If O's paragraph-pages item is enabled, its two bar passes additionally make **two section opens**, each with a forward LUT walk until the paragraph changes (`O Section.cpp:1036–1072`, reader `:3471–3473`). Optional untruncated title sizing can do a further metadata lookup on render (`O reader:2484–2496`). Paragraph numbering is an element walk, not re-layout or a section reload (`:2131–2173`); quote anchors short-circuit when empty (`:2242–2244`). No measured cost is assigned to those features.

O background watermark checks are cheap branches until a build starts; the actual parser work is conditional in both loops. O's foreground bail returns after an eight-page chunk that still cannot satisfy the requested page, then background work eventually requests another render when that page becomes readable. It can improve responsiveness while adding scheduling gaps to time-to-page; it is not a general layout-speed optimization. Sources: O reader `:809–833,2856–2902`, `WatermarkBuildBail.h:1–14`; U reader `:391–404,1318–1343`.

Release performance logging is off by default; O's development profile explicitly forces it (`O platformio.ini:236`, settings `:634–637`). Enabled logging buffers 16 records; the next record flushes 16 CSV lines and commits the file (`PerfLog.cpp:16,80–82,129–145`). Instrumented versus release builds are a real confounder. `Diagnostics::flush()` is not called by the ordinary reader/main loop; its definition alone is not evidence of per-loop card writes.

### Host tests and layout benchmark

Built in `/tmp`, with repository source unchanged:

- O CMake Release targets: `WordSpacingTest` **99 passed**, `display_refresh_policy_tests` **30**, `IdlePrewarmNeighbourTest` **7**, `WatermarkBuildBailTest` **6**, `reader_progress_save_debouncer_tests` **7**: **149 passed**.
- U CMake Release `ChapterHtmlSlimParserTest`: **9 passed**. Its target compiles production `ChapterHtmlSlimParser.cpp`, `ParsedText.cpp`, `Page.cpp`, `TextBlock.cpp`, CSS and bidi helpers (`U:test/chapter_html_slim_parser/CMakeLists.txt:1–45`). O's layout target compiles production `ParsedText.cpp`, hyphenation and bidi (`O:test/word_spacing/CMakeLists.txt:1–25`).
- Neither test tree supplies a complete `Section.cpp` + EPUB archive + real-font/storage host benchmark target. The upstream parser tests stub font/storage/image services; they are correctness tests, not evidence of chapter timing. A comparable **layout-kernel** benchmark was built for both instead. Full `Section` was not benchmarked; that part of the requested comparison remains a limitation.

The benchmark uses O's tracked `test/epubs/font-prewarm-benchmark.epub`, SHA-256 **`fb683591fb2c9f0daee19d88c4013e78b1c2462c2ba85d24684e1bb7a9af8af1`**, for **both binaries**. It extracts every `<p>` from sorted XHTML/HTML archive members, normalizes whitespace, and tokenizes before timing: **482 paragraphs, 3,886 words**. Thus it compares EPUB-derived text, not ZIP inflation, HTML parsing, CSS/table pagination, or the full EPUB ingestion pipeline.

Timed work: construct production `ParsedText`, add all words, justify/extract lines, consume a checksum, destroy objects, for 1,000 corpus passes. Both use width 440, regular style, LTR, no hyphenation/focus reading; O guide dots off, word spacing 100, default indent. Both use the same deterministic renderer stub from O `test/word_spacing/stubs/GfxRenderer.h`, with an ascender accessor added for U. A `TextBlock` constructor stub counts/checks output; actual output-arena construction, storage and rasterization are **excluded in both**. Production layout entry points: O `lib/Epub/Epub/ParsedText.cpp:410,708,1299`; U `:391,681,1255`.

Linux x86_64, GCC 13.3.0, `-O2 -std=c++20 -fno-exceptions -fno-rtti`; bidi C compiled with gcc `-O2`. Process pinned to available CPU 0. One 50-pass warmup each, then nine 1,000-pass runs each, alternating tree order. No claim that this emulates C3 memory pressure or timing.

| Host milliseconds per 1,000 corpus passes | O | U |
|---|---:|---:|
| Minimum | 1284.14 | 1379.80 |
| Median | **1352.65** | **1422.32** |
| Maximum | 1552.99 | 1507.95 |

Raw O samples: `1352.65, 1341.17, 1393.87, 1284.14, 1323.37, 1552.99, 1460.10, 1316.76, 1381.00`.

Raw U samples: `1431.97, 1391.59, 1392.49, 1422.32, 1398.59, 1507.95, 1442.86, 1453.57, 1379.80`.

Each run emitted **1,449,000 lines / 3,886,000 words** in both. Checksums differed: **5,307,090,000 O / 5,300,034,000 U**. Output positioning is not identical despite equal line/word counts; this is a same-input cost comparison, not a proof of layout equivalence. O's lower median and overlapping ranges do not justify claiming a general speedup. They do refute any claim that *this benchmark* measured U faster.

Commands/logs and the complete benchmark source are retained in `/tmp/perfgap-bench/`, `/tmp/perfgap-bench-setup.py`, `/tmp/perfgap-{cmake,tests}-{ours,upstream}.log`, `/tmp/perfgap-policy.cpp`, and `/tmp/perfgap-policy.txt`. Temporary host setup initially lacked two SDK dependencies and used an incorrect test target name; these were corrected, and final targets/tests above passed. Compiler warnings were present; no claim of a warning-free firmware build is made. Reproduction source follows at the end of this report.

### Reproducing the host evidence

All writes below belong in `/tmp`; run from the lector worktree. First populate `/tmp/perfgap-evidence/{ours,upstream}` using `git show` at the two pinned commits for `lib/`, `src/`, `test/`, and `platformio.ini`. For the CMake test configuration also populate each snapshot's `freeink-sdk/` from its pinned gitlink (including `libs/network/NearbyTransfer` and `libs/ui/FreeInkUI` in O). No checkout or firmware build is needed. The following is the actual comparative benchmark setup script; it builds both production layout kernels against the same metrics:

<details>
<summary>Benchmark setup, sampling and refresh-count source</summary>

Save as `/tmp/perfgap-bench-setup.py` and run `python3 /tmp/perfgap-bench-setup.py`:

```python
import pathlib,zipfile,xml.etree.ElementTree as ET,hashlib,subprocess
base=pathlib.Path('/tmp/perfgap-evidence'); bench=pathlib.Path('/tmp/perfgap-bench');bench.mkdir(exist_ok=True)
epub=pathlib.Path('test/epubs/font-prewarm-benchmark.epub')
paragraphs=[]
with zipfile.ZipFile(epub) as z:
 for n in sorted(z.namelist()):
  if n.endswith(('.xhtml','.html')):
   root=ET.fromstring(z.read(n))
   for el in root.iter():
    if el.tag.split('}')[-1] == 'p':
     s=' '.join(''.join(el.itertext()).split())
     if s: paragraphs.append(s)
(bench/'paragraphs.txt').write_text('\n'.join(paragraphs)+'\n')
print('epub sha256',hashlib.sha256(epub.read_bytes()).hexdigest(),'paragraphs',len(paragraphs),'words',sum(len(p.split()) for p in paragraphs))
(bench/'bench.cpp').write_text(r'''
#include <GfxRenderer.h>
#include "ParsedText.h"
#include <chrono>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>
static uint64_t lines=0, wordsOut=0, checksum=0;
TextBlock::TextBlock(const std::vector<std::string>& words, const std::vector<int16_t>& xpos,
 const std::vector<EpdFontFamily::Style>& styles, const std::vector<uint8_t>&,
 const std::vector<uint16_t>&,
#ifdef OURS
 const std::vector<uint16_t>&,
#endif
 const BlockStyle&, std::vector<std::string>
#ifndef OURS
 ,std::vector<LinkSpan>
#endif
 ) {
 ++lines; wordsOut+=words.size();
 for(size_t i=0;i<words.size();++i) checksum+=words[i].size()*31+xpos[i]*7+styles[i];
}
int main(int argc,char**argv){
 if(argc!=3)return 2;
 std::ifstream input(argv[1]); std::vector<std::vector<std::string>> paras; std::string line,word;
 while(std::getline(input,line)){std::istringstream in(line); std::vector<std::string> p; while(in>>word)p.push_back(word);paras.push_back(std::move(p));}
 GfxRenderer renderer; const int repetitions=std::atoi(argv[2]);
 const auto start=std::chrono::steady_clock::now();
 for(int r=0;r<repetitions;++r) for(const auto& p:paras){
  BlockStyle style;style.alignment=CssTextAlign::Justify;style.textAlignDefined=true;style.directionDefined=true;style.isRtl=false;
#ifdef OURS
  ParsedText parsed(false,false,false,0,style);
#else
  ParsedText parsed(false,false,false,style);
#endif
  for(const auto&w:p)parsed.addWord(w,EpdFontFamily::REGULAR);
#ifdef OURS
  parsed.layoutAndExtractLines(renderer,0,440,[](void*,std::shared_ptr<TextBlock>,uint32_t){},nullptr);
#else
  parsed.layoutAndExtractLines(renderer,0,440,[](std::unique_ptr<TextBlock>,uint32_t){});
#endif
 }
 std::cout<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<" "<<lines<<" "<<wordsOut<<" "<<checksum<<"\n";
}
''')
(bench/'stubs').mkdir(exist_ok=True)
gfx=(base/'ours/test/word_spacing/stubs/GfxRenderer.h').read_text().replace(' public:', ' public:\n  int getFontAscenderSize(int) const { return 12; }')
(bench/'stubs/GfxRenderer.h').write_text(gfx)
for tree in ['ours','upstream']:
 root=base/tree
 cmd=['g++','-std=c++20','-O2','-fno-exceptions','-fno-rtti']
 if tree=='ours':cmd+=['-DOURS']
 # Identical deterministic metrics for both, taken verbatim from lector's existing host test.
 cmd+=['-I'+str(bench/'stubs'),'-I'+str(base/'ours/test/css_parser/stubs')]
 cmd+=['-I'+str(root/p) for p in ['lib/Epub','lib/Epub/Epub','lib/EpdFont','lib/MiniBidi','lib/Utf8','lib/Memory']]
 sources=['lib/Epub/Epub/ParsedText.cpp','lib/Epub/Epub/hyphenation/Hyphenator.cpp','lib/Epub/Epub/hyphenation/LanguageRegistry.cpp','lib/Epub/Epub/hyphenation/LiangHyphenation.cpp','lib/Epub/Epub/hyphenation/HyphenationCommon.cpp','lib/MiniBidi/BidiUtils.cpp','lib/MiniBidi/minibidi.c','lib/Utf8/Utf8.cpp']
 subprocess.run(['gcc','-O2','-c',str(root/'lib/MiniBidi/minibidi.c'),'-o',str(bench/(tree+'-bidi.o'))],check=True)
 sources.remove('lib/MiniBidi/minibidi.c')
 cmd += [str(bench/(tree+'-bidi.o')),str(bench/'bench.cpp')]+[str(root/p) for p in sources]+['-o',str(bench/tree)]
 (bench/f'{tree}-command.txt').write_text(' '.join(cmd)+'\n')
 with (bench/f'{tree}-build.log').open('w') as log:
  result=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT)
 print(tree,'build',result.returncode)
```

Save as `/tmp/perfgap-bench/run.py` and run `python3 /tmp/perfgap-bench/run.py` (choose an available CPU automatically):

```python
import subprocess,statistics,os,json,platform
from pathlib import Path
root=Path('/tmp/perfgap-bench');cpu=min(os.sched_getaffinity(0));os.sched_setaffinity(0,{cpu})
results={k:[] for k in ['ours','upstream']};counts={}
for k in results:subprocess.run([str(root/k),str(root/'paragraphs.txt'),'50'],check=True,stdout=subprocess.DEVNULL)
for i in range(9):
 for k in (['ours','upstream'] if i%2==0 else ['upstream','ours']):
  fields=subprocess.check_output([str(root/k),str(root/'paragraphs.txt'),'1000'],text=True).split();results[k].append(float(fields[0]));counts[k]=fields[1:]
out={'cpu':cpu,'host':platform.platform(),'rounds':1000,'samples':results,'counts':counts,'summary':{k:{'min_ms':min(v),'median_ms':statistics.median(v),'max_ms':max(v)} for k,v in results.items()}}
(root/'results.json').write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
```

Policy count harness, saved as `/tmp/perfgap-policy.cpp`:

```cpp
#include <initializer_list>
#include "DisplayRefreshPolicy.h"
#include <cstdio>
int main(){
 using M=DisplayRefreshPolicy::Mode;
 for(int aa:{0,1}) for(int score:{0,300}) for(int turbo:{0,1}){
  DisplayRefreshPolicy p;int cadence=0, fast=0,half=0,full=0,gray=0;
  for(int i=0;i<60;++i){
   M requested=cadence<=1?M::Clean:M::Fast;
   bool t=turbo&&requested==M::Fast&&p.useTurbo(true);
   M actual=p.choose(requested,0,requested==M::Fast?score:0,t);
   fast+=actual==M::Fast;half+=actual==M::Clean;full+=actual==M::Full;
   cadence=cadence<=1?15:cadence-1;
   if(aa){p.noteExternalFastPass();++gray;}
  }
  std::printf("aa=%d score=%d turbo=%d fast=%d half=%d full=%d gray=%d\n",aa,score,turbo,fast,half,full,gray);
 }
}
```

Compile and run against O's production policy:

```sh
g++ -std=c++20 -O2 -Ilib/hal /tmp/perfgap-policy.cpp lib/hal/DisplayRefreshPolicy.cpp -o /tmp/perfgap-policy
/tmp/perfgap-policy
```

Host test commands used after populating snapshots:

```sh
cmake -S /tmp/perfgap-evidence/ours/test -B /tmp/perfgap-build-ours -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/perfgap-build-ours --target WordSpacingTest display_refresh_policy_tests IdlePrewarmNeighbourTest WatermarkBuildBailTest reader_progress_save_debouncer_tests -j 4
ctest --test-dir /tmp/perfgap-build-ours -R 'WordSpacing|Refresh|Prewarm|Watermark|ProgressSave' --output-on-failure
cmake -S /tmp/perfgap-evidence/upstream/test -B /tmp/perfgap-build-upstream -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/perfgap-build-upstream --target ChapterHtmlSlimParserTest -j 4
/tmp/perfgap-build-upstream/chapter_html_slim_parser/ChapterHtmlSlimParserTest
```

</details>

## 3. Ranked real differences, cheapest prospective fix first

This orders likely implementation effort, **not a measured attribution of the owner's delay**. No fixes were applied. Hardware quality and RAM tradeoffs must be preserved.

1. **Align the SdFat bulk-transfer flag.** One build flag is the smallest candidate change; it replaces per-byte SPI calls with buffer transfers. O `platformio.ini:33–75` versus U `:49–56`; driver trace above. It needs the 512-byte write-stack cost checked on C3. U addition: `e33e3cf39d66a35905782866b3cf04a7223b2e31`. This is the cheapest concrete upstream advantage, not a quantified page-turn gain.
2. **Compare equivalent logging profiles first.** O development forces CSV logging (`platformio.ini:223–236`, `PerfLog.cpp:80–82,129–145`); U development has no equivalent forced sink (`platformio.ini:209–217`, `lib/hal/HalDisplay.cpp:62–78`). A release-versus-development mismatch can be removed from the experiment without changing source. If both tested binaries are normal release builds, demote this candidate.
3. **Avoid unnecessary per-render data collection/scanning only after measuring it.** O frame scoring scans 48,000/52,272 bytes even for a caller-requested cleanup (`HalDisplay.cpp:131–135`); U `:62–75` has no scoring. O status-bar chapter fetches can be unnecessary for the selected items (`reader:3448–3473`), unlike U's conditional title branch (`reader:1781–1791`). Do not remove ink-policy state or validation blindly. Relevant O additions: `29eb8f4673b2b8f72c53f179b7faa755436a5db0` and `6caefbfbb3385ef6983458ef3d4a1f9836aaed1b`.
4. **Review the image double-base sequence.** O reader `:3215–3236` versus U `:1589–1594`: one or two avoidable-looking base submissions and one extra content render, depending on cleanup state. U commit `d3b3b5669595c8fd1417d84ee0558e5aa5df2186` provides a concrete simpler path, but grayscale contrast/ghosting must be compared on the actual panel before adopting it. This is potentially a much larger image-page effect than small CPU bookkeeping.
5. **Evaluate the global cleanup policy against image quality.** O policy adds HALF/FULLs independently of the user's reader interval; U lacks this layer. O `lib/hal/DisplayRefreshPolicy.cpp:14–63`, `HalDisplay.cpp:197–205` (promoted async requests become blocking), versus U `HalDisplay.cpp:62–78`. Counted differences above are real, but cleanup exists to control ghosting. O history: cap `749afff1fcefba37e7ca2117b40225d2cc38b8f5`, ink budget `29eb8f4673b2b8f72c53f179b7faa755436a5db0`. On X4, O's Turbo (`3defd12e63f0a434a7b781d8572b8056583d04ba`) may more than offset the added cleans; that balance is unmeasured.
6. **Investigate X3 completion waits, preserving correct settling.** OS driver `Uc8253X3Driver.cpp:91–108,294,333` versus US `:262–310`. Two extra stable-high waits exist even on warm ordinary BW turns. OS commit `ff69449997bc9fdbc62f0af6afea352227796aa8` added them to address multi-pulse BUSY behavior. Removing them without optical/BUSY evidence risks merely reporting completion too early. This is a real SDK difference, not a claim that generic lector `loop()` is slow.

Countervailing changes from **our** history, not candidates to undo: batching progress and reusing scratch `aee032520bc3b1790177a4a9fde8d3ae9cb29812`; direction-aware prewarm `34f7fd3d2e6fefd4d6832ecd7c2a741a4ed4fccc`; yielding watermark extension `aacb049527f5b479e4062beca1706948d8ca4b22`; buffered cumulative-size caching `f3cbd6ff3445b44c179022d15d6c6e23a4536f40`; DFS/tickless management `bb86938c91e5f5a670432f0d3dc66f705f367663`. Their compared current paths are in the evidence table. Commit subjects are provenance, not performance measurements.

## 4. What only hardware can settle

Use the **same C3 device, panel revision, SD card, EPUB bytes and effective settings**, alternating these pinned binaries. Preserve separate cache images for each firmware: their versions are incompatible. Record exact build environment, resolved dependencies, optimization flags, measured runtime CPU/SPI clocks, USB/WiFi state, temperature, and effective per-book AA/font/margins/status-bar settings. Do not compare U's default AA-on with O's default AA-off. Record O Turbo and both refresh intervals explicitly.

Measure these timestamps with a logic analyzer plus, ideally, a photodiode/high-frame-rate video:

1. Physical button closure and the firmware's **accepted page action** (a temporary GPIO marker at O reader `:1018–1021` / U `:675–680`, including U pending-action acceptance `:627`). These markers are a proposed later measurement, not changes made here.
2. Entry to page read, exit from `Section::loadPage()`, font-prewarm end, BW raster end, first EPD transfer/activation, each BUSY edge, final grayscale completion/baseline resync, and first stable readable pixels. Report **accepted-action→stable pixels**, plus input→acceptance separately; do not substitute `renderContents()` duration for end-to-end latency.
3. Count SD CS transactions, payload bytes, progress-write/flush events, and actual EPD refresh commands. Capture requested and HAL-selected mode **and driver effective mode**. A HALF request on X3 can mean three triggers; HAL mode labels alone miss that.
4. Record heap free/largest block and which grayscale tier actually ran. No proposal may assume two extra 48 KB planes fit in the C3's 380 KB RAM; no PSRAM benchmark applies.

Run at least 100 sequential cached text turns per configuration to include the 48-FAST cleanup budget. Include 200–500 ms rapid turns and 3/30-second reading intervals; report median, p90/p99, maximum, refresh-mode distribution, and ghosting/contrast together. Test forward/backward prewarm, both AA states, O Turbo on/off, SD versus built-in fonts, image pages with cold decode and warm pixel caches, chapter boundaries, reopened partials near their watermark, and a genuinely cold section rebuild separately. For a cold-layout run capture parsing/layout, SD writes, and popup refreshes separately from warm page reads.

The first controlled checks should be bulk SPI transfer cost and refresh-count distribution; they distinguish SD work from panel work without inventing a CPU explanation. On X3, capture the BUSY waveform around O's two stability waits and U's earlier returns to determine whether U actually finishes sooner or merely starts subsequent work before settling. On X4, test whether O's Turbo advantage outweighs additional cleanup. For image pages, count the extra O bases and compare final image quality.

O already provides `/perf` requested/actual mode, wire/busy/settle and think-time fields (`src/PerfLogSink.cpp:73–87`, `lib/hal/HalDisplay.cpp:150–162`). Those aid diagnosis but do not replace paired instrumentation: `PerfStats::noteInput()` records a press (`src/main.cpp:1058–1061`), not necessarily the accepted page action, and CSV flushing perturbs card traffic. Perform an optical baseline with diagnostics off, then an instrumented run with logging overhead accounted for equally.

The remaining unknown is the net balance: fewer upstream SPI calls/base refreshes versus lector's faster display clock/Turbo, reduced writes and scratch allocation, and different cleanup/settling policy. Static counts identify where to measure; they cannot decide that balance in milliseconds.
