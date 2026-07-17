# Lector Speed Plan — make everything snappy

Status (updated 2026-07-17, v0.41.0): most of the plan has SHIPPED. Written against baseline v0.9.1 (`4baf6cf`); file/line references below are stale — verify against the tree before acting on any remaining item.

SHIPPED: Phase 1  (BOOT/LOCK ms ledgers, `src/main.cpp`); 2.1 (serial stall killed); 2.3 (JSON pile lazy — only SETTINGS+APP_STATE pre-paint); 2.7 (SD font scan deferred to first use, `SdCardFontSystem::begin`); Phase 3 + 4 (windex verify-first idle index, `SleepWallpaperStage` prestager, O(1) picks — lock latency independent of image count); 5.1 (shared ZipFile per Epub, `Epub::zip()`); 5.2 partial (section HTML streams at 4KB); 5.3 (audited 2026-07-17: all 14 cache-key fields genuinely alter serialized layout, none draw-only; current+previous generation dirs already make settings round-trips rebuild-free); 5.4 (`pumpWholeBookWarm` background rebuild); wake hold-boot + phantom-click rules (v0.24/v0.31) cover the perceived-wake side of 2.5.

REMAINING (device-test gated unless noted): 2.2 custom bootloader SKIP_VALIDATE; 2.4 display-init/SD overlap; 2.5/2.6 frame-restore wake face + background book re-open; 5.5 double-activity handoff fold; 5.6 close-path value-change guards; 6.1 async refresh; 6.2 windowed menu updates; 6.3 qio + selective -O2; 6.4 idle throttle tuning; 6.5 X3 SPI probe + row-flip; 6.6 batched SPI command writes.

Targets, in Diogo's priority order: (A) lock/unlock, (B) book open/close, (C) thousands of wallpapers with non-repeating rotation, (D) global UI snappiness.

Every phase ends with a device-verifiable check.

---

## 0. Hardware ground truth (what we cannot change)

| Fact | Consequence |
|---|---|
| Deep sleep pulls GPIO13 battery-latch low → **full power-off, RTC domain included** (`lib/hal/HalPowerManager.cpp:63-95`) | Every unlock is a **cold boot**. RTC memory, wake stubs, and deep-sleep bootloader shortcuts are useless on battery. Wake optimization = cold-boot optimization. |
| ESP32-C3: single core 160 MHz, ~380 KB RAM, no PSRAM, **16 KB flash cache** | Hot code pays flash-cache misses; RAM structures must stay small. |
| E-ink refresh is panel-timed: FAST ~0.4–0.5 s, HALF ~1.7 s, X3 FULL ~770 ms ×2 on cold init | A refresh is the hard floor of any visible transition. We can only (a) do fewer/smaller refreshes, (b) overlap CPU work with them. |
| Display + SD share one SPI bus (SCLK=8, MOSI=10, MISO=7) with separate CS (`lib/hal/HalGPIO.h:6-14`) | SD and display transfers serialize. BUT: during the panel waveform (BUSY low period) the bus is idle — SD work CAN run then. |
| X4 = SSD1677 @ 40 MHz SPI; X3 = UC81xx-class @ 16 MHz + double 48KB row-flip per plane (`EInkDisplay.cpp:527, 689-709`) | X3 is structurally slower; some wins are X3-specific. |
| Observed cold boot ≈ 3 s on CrossPoint-class firmware | That is the budget we attack in Phase 2. |

---

## Phase 1 — Measure first (cheap, do before everything)

Add a boot/lock timing ledger so every later phase proves its win on device.

1. Millis-stamped checkpoints in `setup()` (`src/main.cpp:304-546`): after serial, after `Storage.begin`, after settings block, after `setupDisplayAndFonts`, after first paint request. One `LOG_INF` line at the end: `BOOT ms: serial=.. sd=.. json=.. disp=.. paint=..`.
2. Same for `enterDeepSleep()` (`src/main.cpp:227-260`): state save, sleep-screen render, framebuffer save, total.
3. Reuse existing timers already in code (`Epub.cpp:481-528`, `GfxRenderer.cpp:1362-1365`, `pollBusy` timing).

Effort: tiny. Risk: none. Verify: serial log shows ledger on wake and lock.

---

## Phase 2 — Unlock (wake) speed  — target: cut ~1.0–1.5 s off cold boot

Ranked by win/effort:

**2.1 Kill the serial stall — `delay(250)` at `src/main.cpp:313`.**
Runs on every wake of logging builds. Make it conditional: skip when wake reason is PowerButton and no USB attached (check `Serial`/VBUS), or drop to 0 in `gh_release`. Win: 250 ms flat. Risk: none for release users.

**2.2 Skip bootloader image validation on power-on.**
The 2nd-stage bootloader CRC-checks the ~6.5 MB app image every cold boot. `CONFIG_BOOTLOADER_SKIP_VALIDATE_ON_POWER_ON=y` skips it (the deep-sleep variant is irrelevant here — sleep is power-off). Needs a custom-built bootloader binary because Arduino/PlatformIO ships a prebuilt one; pioarduino supports `board_build.bootloader` override / custom sdkconfig via esp-idf component build. Win: estimated 200–500 ms on a 6.5 MB image at 80 MHz DIO. Risk: a corrupted flash image boots instead of failing verify — acceptable for an e-reader with OTA fallback partition; document it. This is the deepest single boot win available.

**2.3 Defer the JSON pile.**
`main.cpp:352-358` loads 7 SD JSON files serially before any pixel. Only `SETTINGS` and `APP_STATE` (for `openEpubPath` + boot routing) are needed pre-paint. Move `RECENT_BOOKS`, `KOREADER_STORE`, `OPDS_STORE`, theme reload to lazy first-use (home activity onEnter / settings entry). Win: ~4 fewer SD file open+parse before first paint (tens of ms each, more on slow cards). Risk: low; add lazy-load guards.

**2.4 Overlap display init with SD work.**
`setupDisplayAndFonts` (`main.cpp:262-289`) runs after `Storage.begin` + JSON. Display reset/init contains long BUSY waits where the SPI bus is free. Restructure: kick display `begin()` on the render task immediately after GPIO init, do SD mount + settings load on main task, join before first paint. Both drivers already share the bus safely only via mutexes — the display init path must take the same storage/bus mutex around its SPI *transfers* (not its BUSY waits). Win: hides most of SD mount + JSON time inside display init (likely 100–300 ms). Risk: medium — bus arbitration must be right; gate behind a build flag first.

**2.5 Make quick-resume the default wake face.**
`loadSleepFrameBuffer` (`main.cpp:212-224`) restores the saved 48 KB frame with ONE refresh — the fastest visible wake that exists today. Currently only in quick-resume sleep mode. Extend: on wallpaper-sleep wakes, keep the current "seamless + 1-bit wallpaper redraw" path (already good); on reader-sleep wakes, prefer frame restore so the user sees their page instantly while the book loads behind it (see 2.6). Win: perceived wake ≈ display init + 1 fast refresh.

**2.6 Background book re-open after first paint.**
Today wake routes to `goToReader` (`main.cpp:523`) and the user waits for `Epub::load` + section header read + page render + refresh. With 2.5, paint the restored page frame FIRST, then do `Epub::load` + section load while the user already sees their page; only refresh again if content differs (it will not, same page). Win: book-resume wake feels near-instant; real work hides behind the already-correct frame. Risk: medium — must invalidate the saved frame when settings/orientation changed while asleep (compare a small hash of render-relevant settings saved next to the frame).

**2.7 Defer SD font scan.**
`sdFontSystem.begin` dir-scan (`main.cpp:286`, `SdCardFontSystem.cpp:18-46`) → move to first reader entry (`ensureLoaded` already exists at `ReaderActivity.cpp:127`). Win: small, free.

Phase 2 verify: boot ledger before/after on both X3 and X4; target ≤ 1.5 s from button to visible page on X4 reader-resume.

---

## Phase 3 — Lock (sleep entry) speed — target: button-press → dark in ≤ 1 refresh + ~0.3 s

**3.1 Pre-pick and pre-stage the next wallpaper at WAKE-idle, not at lock.**
Everything slow at lock (folder scans, order-file streaming, cursor logic, `saveToFile`) moves to a background step ~10 s after wake while the user reads. At lock: read the pre-staged path from `APP_STATE`, render, sleep. This alone removes ALL scan latency from the user-blocking path and is the enabler for Phase 4's thousands-of-images goal. Win: lock latency becomes decode+refresh only. Risk: low; if pre-stage missing (first boot), fall back to current path.

**3.2 Go further: pre-render the sleep frame to a file at idle.**
At idle, decode the chosen wallpaper into the framebuffer, save as `/.crosspoint/next_sleep_frame.bin` (48 KB, same mechanism as `saveSleepFrameBuffer` `main.cpp:203-210`), restore user's screen after (or render to a heap buffer if 48 KB fits at that moment). At lock: load 48 KB + one refresh. Lock cost ≈ 48 KB SD read (~50 ms) + refresh. This also makes grayscale free: do the 3-pass grayscale composite at idle where nobody waits, store the final planes. Risk: medium (single-framebuffer juggling); design carefully or accept 1-bit pre-render first.

**3.3 Collapse the redundant state writes.**
Lock path writes `state.json` 2–3× (`main.cpp:237`, cursor persist in `Wallpaper.cpp:56`, `SleepActivity.cpp:234`). Batch into ONE `saveToFile` at the end of `enterDeepSleep`. Win: 1–2 SD writes. Risk: none.

**3.4 Drop the "Entering sleep" popup refresh when a wallpaper will paint anyway.**
Each popup is an extra e-ink refresh (~0.5 s). Show it only in slow fallback paths. Win: one refresh. Risk: none.

**3.5 Optional setting: 1-bit sleep screens.**
Bayer-dithered 1-bit path already exists (`PxcSleepRenderer.cpp:104-135`) and costs ONE fast refresh vs 3-pass grayscale + gray composite. Expose "sleep image quality: fast (1-bit) / pretty (grayscale)". With 3.2 in place, grayscale is prerendered and this matters less; ship as stopgap earlier.

Phase 3 verify: lock ledger; target: press → final sleep image in ≤ 1.2 s with grayscale pre-render, ≤ 0.8 s with 1-bit.

---

## Phase 4 — Wallpapers at thousands, non-repeating, flawless

Current design self-trims `/sleep` to **500 images** (`kSleepFolderCap`, `src/sleep/Wallpaper.h:31`) and does 2–4 full FAT directory scans per lock (`Wallpaper.cpp:124-151, 258-330`; `WallpaperPlaylistV2.cpp:374-400`). At 5000 files that is ~0.5 MB of directory reads per scan and an O(N×500) compare storm — seconds of user-blocking time, plus 4500 images silently exiled to `/sleep pause`. The system needs a redesign, and the user has approved changing it. Replace both engines (buffer + direct-pick) with one manifest-based engine:

**4.1 Persistent manifest: `/.crosspoint/sleep_manifest.bin`.**
Binary file: header (version, count, cursor position, shuffle seed, dir fingerprint) + fixed-size records (name hash u32, flags: favorite/exists, name offset) + name blob. 5000 entries ≈ 5000×(4+2+2) + ~100 KB names ≈ **~140 KB on SD, never fully in RAM** — read records in 4 KB windows. Order IS the file order (shuffled at build time with Fisher-Yates, or newest-first per setting).

**4.2 O(1) rotation.**
At pick time: cursor++, read one record + its name, one `exists()` check (skip-and-advance if deleted), done. No directory scan, no order-file line streaming, no 500-name compares. Non-repeat guarantee: cursor laps the full manifest — every image shows exactly once per lap, including 5000-image laps. Reshuffle at lap end (new seed, rewrite manifest header + record order at idle).

**4.3 Incremental reconcile at idle (never at lock).**
Detect changes cheaply: store dir fingerprint (file count + a rolling hash of names+mtimes from ONE scan). At wake-idle, do a single WDT-yielded scan of `/sleep`; if fingerprint differs, rebuild/patch the manifest in the background (new files splice at front, deleted files flagged dead). One scan per change, zero scans per lock. Favorite renames (`_F` suffix) patch the record in place (keeps position — preserves the v0.9.0 no-reshow behavior).

**4.4 Remove `kSleepFolderCap` auto-trim.**
With the manifest, RAM never depends on N. Keep `/sleep pause` and the bulk-move features as user tools, delete the silent auto-demotion (`trimToCap`). Also fix bulk-move O(N·M/128) re-scan (`SleepFavoriteMove.h:65-73`) to a single-pass move using the manifest.

**4.5 Delete the legacy machinery** once stable: `sleep_order.txt`, split engines, `lastDirectPickFilename` dead field. Keep a one-time migration: on first boot with new firmware, build manifest from existing order file so the rotation position survives the upgrade.

Effort: the big one of this plan (new module + migration + host tests). Risk: medium; mitigate with the existing host-test harness (`test/` wallpaper rotation tests) extended to 5000-entry synthetic manifests.

Phase 4 verify: host tests for lap-completeness (5000 images, no repeat within lap, survives deletes/adds/favorites mid-lap); device test with 2000+ real pxc files: lock latency unchanged vs 50 images.

---

## Phase 5 — Book open/close

Warm opens are already cheap (section header + one page read). The pain is cold builds and avoidable overhead:

**5.1 Share one ZipFile per open.**
`Epub` constructs a throwaway `ZipFile` per call (`Epub.cpp:816-837`), each re-scanning the ZIP central directory. Keep one `ZipFile` member alive for the life of the `Epub` object so its central-dir cursor cache (`ZipFile.cpp:126-174`) actually works. Win: big on cold index (O(spine) rescans gone) and on every chapter fetch. Risk: low; watch file-handle count (6 concurrent max).

**5.2 Raise I/O chunk sizes.**
1 KB read/parse buffers everywhere (`ChapterHtmlSlimParser.cpp:25`, `ZipFile.cpp:400-411`, `Epub.cpp` streams; SD `readFileToBuffer` 64-byte chunks `SDCardManager.cpp:134`). Raise to 4–8 KB where heap-gated (these are transient buffers; 8 KB is affordable outside low-memory tiers). Fewer SdFat transactions = fewer mutex cycles + better SD throughput. Win: measurable on cold builds and chapter loads. Risk: low.

**5.3 Smarter cache invalidation.**
The 14-field section cache key (`Section.cpp:14-34`) wipes ALL page caches on ANY of those settings changing. Split: settings that change layout (font, size, spacing, margins, viewport) legitimately invalidate; others should not be in the key if they only affect draw style. Audit each field; for genuine layout keys, consider keeping the last TWO keyed cache generations per book (disk is cheap, 16 MB+ SD) so toggling a setting back does not force a rebuild. Risk: low-medium (cache versioning discipline).

**5.4 Rebuild sections in the background after settings change.**
After a layout-setting change, current chapter rebuilds while the user waits at the "Indexing" popup. Reuse the existing prefetch pump (`pumpNextChapterPrefetch`, `EpubReaderActivity.cpp:1317-1443`) to also rebuild the current book's remaining sections at idle so later chapter jumps are warm.

**5.5 Fold the double-activity handoff.**
`ReaderActivity::onEnter` fully parses the Epub, then constructs `EpubReaderActivity` which re-does setup (`ReaderActivity.cpp:119-156` → `EpubReaderActivity.cpp:179-240`). Pass the loaded `Epub` object through (already done via pointer) but move `sdFontSystem.ensureLoaded` and orientation work so nothing runs twice; measure, then cut what repeats. Win: small-medium.

**5.6 Close path is already light** (6-byte progress, stats, one state write). Only touch: skip `RECENT_BOOKS` + `APP_STATE` rewrite when nothing changed (value-change guards).

Phase 5 verify: instrumented cold-open and warm-open times on a large EPUB (400+ spine) before/after; settings-toggle round-trip no longer triggers full re-index (5.3).

---

## Phase 6 — Global snappiness substrate

**6.1 Overlap CPU/SD work with e-ink refresh (the biggest structural win).**
`pollBusy` blocks the render task for the whole 0.5–1.7 s waveform (`EInkDisplay.cpp:580-618`). After `CMD_MASTER_ACTIVATION` the SPI bus is idle — only the BUSY pin matters. Add an async refresh mode: `displayBufferAsync()` returns after activation; a `waitRefreshDone()` joins before the next SPI touch of the display. Then: page-turn prefetch (`pumpNextChapterPrefetch`) and SD I/O run DURING the refresh instead of after it. Reader page turns become: render next page into buffer while panel still flushes the previous one. Win: hides up to ~0.5 s of work per page turn; benefits every activity. Risk: medium-high (bus discipline, the 10 ms loop-delay ghosting constraint at `main.cpp:678-684` must be re-validated); implement behind a flag, X4 first.

**6.2 Windowed partial updates for menus.**
`displayWindow` partial path exists (`EInkDisplay.cpp:1656-1717`) but menus repaint + refresh the FULL panel per cursor move (`HomeActivity.cpp:373-378`). Add a lightweight dirty-rect: activities declare the changed row rect (old selection + new selection); renderer sends only those RAM rows and triggers a windowed FAST refresh. Win: menu cursor moves drop from ~0.5 s full-panel to ~0.2–0.3 s small-window, and look calmer. Risk: medium (EXPERIMENTAL driver path; per-call `std::vector` alloc at `:1677` should become a reused buffer). Do Home + Settings + FileBrowser lists first.

**6.3 Build flags.**
- `board_build.flash_mode = qio` (from `dio`, `platformio.ini:47`) — 2× instruction-fetch width; module supports it per ESP32-C3 norm; test-flash carefully with fallback image ready.
- Selective `-O2` on hot TUs (layout: `ParsedText.cpp`, `ChapterHtmlSlimParser.cpp`; render: `GfxRenderer.cpp`, `EInkDisplay.cpp`; pxc decode) via `build_src_flags` or per-file pragmas; keep `-Os` globally for size.
- Win: diffuse but real on this flash-cache-starved chip (16 KB cache, all code XIP). Risk: low; verify image still fits partition.

**6.4 Idle throttle tuning.**
CPU drops to 10 MHz after 3 s idle (`HalPowerManager.h:31`, `main.cpp:668-676`); first press after a pause pays ramp + up to 20 ms poll. Raise `LOW_POWER_FREQ` to 40 MHz and/or restore full speed on the GPIO edge before debouncing completes. Win: removes the "first press after pause feels dead" effect. Risk: minor battery cost; measure.

**6.5 X3-specific.**
- Probe display SPI at 20 MHz (papyrix reports corruption at 20, CrossPoint ships 16 — test on device, keep 16 if artifacts).
- Kill the double 48 KB row-flip per plane (`EInkDisplay.cpp:689-709`): flip during blit into a small line buffer instead of two full-buffer passes.

**6.6 Micro: batch per-byte SPI command writes** (`sendData` per-byte transactions `:629-636`) into small buffered writes during LUT loads and init. Small win, trivial risk.

Phase 6 verify: page-turn ledger (input → refresh-done), menu cursor-move latency, before/after; ghosting visual check after 6.1/6.2 across 50 mixed refreshes.

---

## Suggested execution order (impact-first, device-test gated)

| Step | Items | Why first |
|---|---|---|
| 1 | Phase 1 + 2.1 + 3.3 + 3.4 + 5.1 + 5.2 + 6.3 | Cheap, low-risk, measurable immediately |
| 2 | Phase 3.1 (idle pre-pick) + 4.1–4.5 (manifest) | Unblocks the thousands-of-images goal AND fixes lock latency at the root |
| 3 | Phase 2.2 (bootloader) + 2.3 + 2.5 + 2.7 | Cold-boot attack |
| 4 | Phase 3.2 (pre-rendered sleep frame) + 2.6 (background book re-open) | The "instant" feel for lock/unlock |
| 5 | Phase 6.1 (async refresh) + 6.2 (partial menus) | Structural; biggest everyday-feel win, highest care |
| 6 | Phase 5.3–5.5, 6.4–6.6 | Polish |

Every step lands as its own PR, CI-green, then device test on both X3 and X4 before the next.

## Expected end state (honest estimates, to be proven by the Phase 1 ledger)

- Unlock to visible page: ~3 s → **~1.2–1.5 s** (2.1+2.2+2.3+2.5), feels instant with 2.6.
- Lock to sleep image: multi-second with big folders → **≤ 1 s**, independent of image count (3.1/3.2 + Phase 4).
- Wallpapers: 500-cap removed, **5000+ images rotate non-repeating per lap**, zero lock-time cost.
- Warm book open: already fast → slightly faster; cold open: noticeably faster (5.1/5.2), and settings toggles stop nuking caches (5.3).
- Page turns and menus: work overlaps refresh, menus use small windowed refreshes → **everything feels ~2× snappier**.
