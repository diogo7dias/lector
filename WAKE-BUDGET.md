> **Superseded:** Quick Resume and its timeout override have now been removed.
> Existing Quick Resume selections migrate to Light. The Quick Resume budget and
> guarded experiment below are historical; ordinary-wake timings still apply.

# Wake budget — phase 1, before code changes

Baseline: `46171d938`, branch `perf/unlock-2s`. No attached device; no new
physical timing measurements. Line references in this baseline section refer to
that commit. Values quoted from comments are historical observations, not a
validated floor for all panel batches or temperatures.

## Endpoint and conclusion

Readable means the destination book/home frame has completed its panel waveform,
not that routing returned or a loading banner became visible. First accepted
input is a separate endpoint; the user's delay before pressing a key is not
firmware latency. A Quick Resume page remains physically readable throughout
sleep, but its moon removal and active reader still require wake work.

**No verified two-second result, and no proof that two seconds is impossible on
all devices.** The minimum is panel/path dependent and cannot be established
exactly without device measurements. A true X4 SSD1677 FULL + standard FAST
sequence would spend approximately 1800 + 500 = **2300 ms in panel work alone**;
with banners the 800 ms floor makes that approximately **3100 ms**, before other
work. However, the checked-in SSD1677 driver unconditionally selects HALF for
its first non-turnOff submission (Ssd1677Driver.cpp:448-458), even when the caller
requested FULL. Therefore those are **conditional budgets, not this checkout's
proven floor**. HAL's enum comment calls HALF 1720 ms (HalDisplay.h:19), which
would imply ~2220 ms for HALF + standard FAST, but is not a validated waveform
measurement either. Measure the first submission; do not fix or weaken that
pre-existing driver behavior while chasing unlock speed.

Two seconds remains plausible for cached monochrome Quick Resume after removing
one X3 submission. The historical 2006 ms X3 painted-face total in main.cpp:767
is not a current guarantee: HalDisplay.cpp:99-101 separately cites X3 FULL 2016,
HALF-with-resync 2551 and FAST 617 ms. Those sequences plainly do not support
using the older 710 ms FULL comment as today's unconditional budget. UC8279 /
UC8179 batches and X4 Pro have no validated per-mode numbers here. Banners-off
already overlaps the recovery settle with the blank. Startup, content complexity,
anti-ghost policy, rail power topology and temperature can all raise the floor.

## Complete critical path

| Step | Baseline source | Estimated ms | Evidence / qualification |
|---|---|---:|---|
| Button energizes rail or deep-sleep wake, ROM/bootloader/framework | src/main.cpp:505; lib/hal/HalGPIO.cpp:283 | unknown | No running application ISR timestamp survives a rail cut. Measure button edge externally; millis starts later. |
| Serial startup | src/main.cpp:513-539 | 250 on non-DEEPSLEEP; 0 fixed delay on DEEPSLEEP | Literal delay(250). Battery latch power-on can classify as PowerButton later and still pays this. USB enumeration must remain reliable. |
| System initialization, panic classification | src/main.cpp:541-555 | unknown | HAL startup, RTC marker reads. |
| GPIO, panel probe, power manager and clock | src/main.cpp:557-565 | unknown | HAL hardware initialization; starts recovery deadline. |
| SD mount | src/main.cpp:578-611; lib/hal/HalStorage.cpp:27 | unknown | SPI SD on C3 (SDCardManager.cpp:102 defaults to 40 MHz unless board config overrides); native SDMMC on Pro (:19-54). Card dependent. Mount failure retries 1000 ms up to five minutes, outside normal wake target. |
| Panic/OTA audit, folders, settings/state/recents/credentials/presets | src/main.cpp:617-663 | unknown | SD reads, possible migration writes, JSON decoding. Not a defensible fixed number. |
| Optional prior timing file / perf sink | src/main.cpp:669-680 | unknown | Setting-gated SD I/O; kit forces timings on. Existing serial line describes PREVIOUS wake. |
| Wake classification and power debounce | src/main.cpp:686-715; lib/hal/HalGPIO.cpp:226-245,283-310 | at least 10 for verified button | Literal 10 ms stability window plus pending debounce and hardware/USB queries. |
| Display begin, controller RAM reset, fonts | src/main.cpp:770; lib/hal/HalDisplay.cpp:15-37; freeink-sdk/libs/display/FreeInkDisplay/src/FreeInkDisplay.cpp:109-175 | unknown | Historical main.cpp:801 says 138 X3 / 50 X4 for display bring-up. Font discovery includes SD. Seamless skips initial resync, not RAM initialization. |
| Painted-face async blank, banners off | src/main.cpp:786-789,932-935 | unknown current; historical X3 710 / X4 1809 | main.cpp:773-775; FULL, one submission. Runs during settle when async supported; blocking fallback does not overlap. |
| Recovery chord settle | src/main.cpp:414-431,804-811 | max(0,500 - elapsed since gpio.begin), Pro max(0,20 - elapsed), polling overshoot | Literal deadline; 10 ms polls, confirming sample separated by 6 ms. NOT an unconditional extra 500. Can return early on confirmed chord. |
| Book selection / crash-route checks | src/main.cpp:816-825,975-1078 | unknown | SD exists/recents/folder probe; dirty wallpaper index may scan. No wallpaper decode on ordinary wake. |
| Quick Resume state save + saved frame read/removal | src/main.cpp:254-263,857-863 | unknown | One framebuffer: X3 52,272 bytes, X4/Pro 48,000; SD read, size validation, remove. FrameLoaded milestone also includes state save. |
| X3 baseline restore | src/main.cpp:873-879 | unknown | Controller RAM copies, ZERO panel submissions. |
| Quick Resume restored-frame push | src/main.cpp:883-894 | X3 FAST unknown; X4/Pro 0 | One X3 grayscale-base FAST; already no X4 HALF when no banners. Historical HAL comment gives FAST 617 ms, other main comments ~500; not interchangeable panel measurements. |
| Painted-face blank plus optional unlock text | src/main.cpp:940-950 | unknown actual; FULL reference ~1800 X4 | One FULL request (SSD1677 first-call override described above). Banners share blank framebuffer. Clearing request retained. |
| Unlock banner visibility floor | src/main.cpp:242,250-251; lib/GfxRenderer/GfxRenderer.cpp:displayBuffer | max(0,800 - time since blank completed) | Literal 800 ms deadline, additional to first panel pass; next paint waits only remainder. Quick Resume and straight-to-book do not arm it. |
| Missing Quick Resume frame / cold boot splash | src/main.cpp:898-905,955-956; src/activities/boot_sleep/BootActivity.cpp | unknown | Fallback splash or clean home; not the cache-hit path. |
| Destination routing and book loading | src/activities/ActivityManager.cpp:233-340; src/activities/reader/ReaderActivity.cpp:36-90 | unknown | onEnter, metadata/layout/font cache reads; uncached EPUB indexing can take seconds. ActivityUp at main.cpp:1080 is NOT proof of completed render-task paint. |
| BusyBanner | src/components/BusyBanner.h:DEFAULT_DELAY_MS; src/components/BusyBanner.cpp:39-57 | 0 fixed wait; extra refresh if shown | 400 ms threshold, no sleep. Uncached EPUB shows immediately. Each shown banner adds one FAST request, possibly driver-promoted. |
| First book B/W paint | src/activities/reader/EpubReaderActivity.cpp:3137-3170; ReaderActivity.cpp:36-41; lib/hal/HalDisplay.cpp:161-174 | X4 standard FAST ~500; HALF unknown | Reader countdown selects FAST/HALF; anti-ghost debt and driver initial state may promote. Explicit X3 HALF requests resync and may contain several physical waveform activations. |
| Images / text antialiasing | EpubReaderActivity.cpp:3109-3350; XtcReaderActivity.cpp:429-465 | unknown additional waveforms + decode | Images: two FAST bases, optional pending HALF and decode placeholder; grayscale composite adds one, text AA adds composite after B/W. X3 precondition after a HALF AA base adds one submission (EpubReaderActivity.cpp:3173-3174; XtcReaderActivity.cpp:429-430). |
| Frame completion, input dispatch | src/activities/ActivityManager.cpp:149-200,233; src/main.cpp:1210-1255,1439 | unknown | Rendering task can finish after setup; held wake-power release is suppressed. Actual next press is user dependent. |

## Submission inventory (before edits)

Counts below mean calls that submit a refresh, not framebuffer clears or LSB/MSB
uploads. Grayscale wallpaper/cover/transparent bases use `SleepGrayscaleBase.h:19-23`:
HALF for X3 and SSD1677/UC8179; FULL for non-X3 UC8279. Stats dashboard instead
explicitly requests HALF for its grayscale base (StatsDashboardRenderer.cpp:163).
These distinctions affect waveform strength, not the count of base plus composite.
A driver may implement one HAL submission with several waveform
activations; HAL count must not be called an exact electrical activation count.

| Sleep face | Lock submissions, ALL X3/X4/Pro | Wake before destination |
|---|---|---|
| Quick Resume, including timeout override | 2: X3 FAST grayscale bases; X4/Pro HALF twice (SleepActivity.cpp:1413-1428) | X3 1 FAST base; X4/Pro 0; no banners |
| Blank | 2 HALF (SleepActivity.cpp:734-758,1431) | 1 FULL request blank or blank+banners |
| Light / default crest | 2 HALF for Light; popup + HALF for other default values (734-758,1148-1170) | 1 FULL request blank or blank+banners |
| Custom BMP/PXC, cover, cover-custom | popup + 1 monochrome face = 2; popup + grayscale base + composite = **3** (1214-1235; PxcSleepRenderer.cpp:202-257) | 1 FULL request; no sleep image decode |
| Stats dashboard | popup + face pipeline; monochrome 2, grayscale 3 via StatsDashboardRenderer.cpp:156-175; fallback crest 2 | 1 FULL request |
| Transparent custom | preserved-frame popup + monochrome 1 or grayscale base/composite 2 = 2 or 3 (SleepActivity.cpp:699-711,511-531,1283-1301) | 1 FULL request; this is not Quick Resume |
| Missing/corrupt artwork fallback | popup + default crest = 2; partial decode failure can already have submitted before falling back | 1 FULL request |

Thus the comment “every lock is two” is not an exact count for grayscale faces:
a grayscale base plus composite is TWO, and their preceding popup is a THIRD.
`afa18d538`'s lock repetition remains untouched. It runs before sleep, not after
the wake press, and cannot be credited as an unlock saving.

Destination adds: ordinary monochrome reader/home 1; text-AA reader 2 (+1 X3 precondition after a HALF base); EPUB image
page normally 2 FAST bases + 1 composite, plus optional HALF clean and placeholder;
XTC B/W 1, grayscale 2 (+1 X3 precondition after a HALF base). Additional BusyBanners/fallback renders count separately.

Device selection is in FreeInkDisplay.cpp:109-175: X3 UC8253 or UC8279d; X4 and
Pro SSD1677, UC8179 or UC8279 800x480 according to linked drivers and probe.
SSD1677's first non-turnOff submission becomes HALF, including a requested FULL; UltraChip invalid OLD-plane/initial
clear flags force clean work (Uc8179Driver.cpp:292-293,
Uc8279Driver.cpp:120-174; Uc8279X4Driver.cpp). These variants share HAL submission
counts but NOT waveform durations. SSD1677 defaults document FULL ~1800 and PART
~500 in Ssd1677Driver.cpp:49-51; this is a source comment, not a datasheet guarantee.
No oscillator/temperature conversion from LUT bytes alone establishes milliseconds.

## Ranked costs and candidates

1. Panel waveforms, ~500-1800 ms each on documented X4 modes. Preserve required
   FULL wallpaper clear. X4 Quick Resume double HALF is already removed here,
   so saving it again is **0 ms**. X3 still redundantly submits the retained frame
   before rebuilding the destination; omitting that is a candidate ~500-617 ms
   saving on historically measured X3 FAST, guarded for device ghosting validation.
The UC8253 X3 driver also has fixed work omitted by the old top-level comments:
   Uc8253X3Driver.cpp:300 waits 200 ms after non-FAST display; :341-347 adds a
   no-op FAST after FULL sync; :303-320 can add conditioning passes on explicit
   HALF resync. Its stable-idle drain (:94-109) requires 8 ms continuous idle (header:99), not
   just one BUSY edge. These are inside the logged HAL duration, not additional
   HAL submissions. First-busy-assert loops on the UltraChip drivers are bounded
   at 50 ms, not guaranteed 50 ms delays. SSD1677 begin has a fixed 10 ms reset
   settle (:181-184). No datasheet-derived exact duration is available here.
2. Unlock banner floor: up to 800 ms residual, intentional visible-title behavior.
   Existing straight-to-book setting already removes it; no duplicate setting.
3. Recovery deadline: up to 500 ms less existing work; often fully hidden by blank.
   Hardware calibration matters. Keep window until measured residual justifies
   shortening it; arbitrary 500->20 would risk recovery false positives.
4. Serial: 250 ms on rail-cut boot, not on actual deep-sleep reset. Keep USB
   enumeration protection pending device evidence; not a free unconditional cut.
5. SD/config/layout/rendering: unknown; uncached book can dominate, ordinary
   lookups do not justify speculative optimization ahead of whole waveforms.

Lower-bound model (cached monochrome): boot/SD/display + max(residual settle,
FULL blank) + page preparation + page waveform with banners off; with banners,
settle + FULL + max(800,page preparation) + page waveform. For Quick Resume:
boot/SD/display + residual settle + frame/baseline + retained-frame submission
(if any) + destination preparation/paint. Unknown terms prevent an exact minimum.

Device validation must measure from button edge to completed readable page (video
or logic analyser), report controller identity, temperature, sleep face, banner
setting, cache state and anti-ghost budget. Serial software totals omit power-up
before its timebase; a BUSY completion is an electrical proxy, not optical proof.

## Phase 2 — release instrumentation

`WakeTiming` now records exact config-read, classification/debounce, frame-read
and recovery-settle durations in RAM. Existing `PerfStats::noteRefresh` captures
each HAL submission elapsed time, including grayscale base/composite and async
completion. The HAL now also records explicit grayscale preconditioning when
BUSY accounting shows actual work (X3); no-op driver paths do not add a submission.
That pass was absent from the existing perf sink, despite driving the panel.
An outstanding async record is closed before resetting its accounting. Capture freezes after the first destination reader/home render that
actually submits a frame, not when setup merely routes there. The render task
publishes completion with a release/acquire atomic for the dual-core Pro.

At the next dispatch iteration, `SLP Wake readable=...` reports milliseconds from
the first setup statement and input-dispatch readiness. Once a first exposed
mapped key edge, touch tap/down/release/long press or menu/home gesture has been recorded, it prints one `SLP Wake ...
ms total classify=... cfg=... frame=... settle=... panels[N]=a/b/... first_input=...`
line. The first-input timestamp includes the user's wait; it means an event
exposed to activity dispatch, not proof that a particular activity acted on it.
Raw suppressed wake-release edges are excluded. Existing input queries report
their result unchanged; the logger never polls a consuming gesture or hint query. An event before
readable completion is retained too, so an early-input race is visible as
`first_input < total`. The line is emitted on the next dispatch after completion. No next input means no first-input
line; the readable line still reports automatically. Cold boot also reports.

All units are milliseconds. `frame=0` means no saved-frame attempt (or <1 ms);
classify includes the HAL query plus verified-button debounce, cfg is the actual
settings/state/recents/store/preset block, settle is the actual recovery check.
They are not an exhaustive partition of total: SD mount, fonts, rendering and
other setup work remain in the remainder. Panel elapsed time includes transfer,
BUSY and driver post-work. Async panel time overlaps settle: **do not add them**.
The first eight submissions are printed, `N>8` exposes overflow; individual times
saturate at 65535 ms. Existing per-refresh CSV supplies extended diagnostics when
timings are enabled. These are HAL submission counts, not internal activation
counts, and their requested/actual HAL labels do not reveal driver overrides.

The new serial data requires neither Performance Timings nor an SD write. It
uses roughly 60 bytes of static state, a 48-byte formatting buffer, and no heap.
It is compiled in gh_release (LOG_LEVEL=1), testkit and x4pro. No changes to
logging transport, strings shown on the panel, translations or flash writes.
ROM/startup before setup and optical completion remain external measurements.

## Phase 3 — guarded cut and device experiment

`LECTOR_FAST_QUICK_RESUME` defaults to 1 in `sleep/WakeFacePolicy.h`. On X3 it
skips the retained-frame FAST grayscale-base submission when no banners changed
pixels. The saved framebuffer is still validated and both controller planes are
restored; the destination reader still gets its existing FAST initial countdown.
X4 and Pro already skipped their unchanged-frame push, so gain there is **0 ms**.
New banners still force a push. All painted-face clearing requests and all lock
submissions stay unchanged. The existing flag-off path restores the X3 pass.

Expected X3 saving: **one FAST grayscale-base call**, provisionally around
500–617 ms using the repository's historical FAST estimates. This is a proxy,
not a measurement of this exact pre-BW-mid LUT: actual saving is the flag-off
retained-frame duration, minus any change in the subsequent destination paint.
Do not call that range a measured speedup. UC8253 and UC8279d can differ.
Repeated identical-page conditioning can affect retained charge, so code
inspection alone cannot establish visual safety. A/B builds must compare both
latency and ghosting, including several page turns after wake.

Leave the 500/20 ms hardware settle deadlines in place: its exposed cost is only
the residual, and changing the calibration before seeing `settle=` is unjustified.
Keep the 250 ms cold serial delay and the explicit banner floor; use the existing
straight-to-book setting for the latter. Skip SD micro-optimizations: no defensible
millisecond gain is established for them. Lock Lab overrides can add preclears
or omit face passes in kits; test with its ordinary/default full recipe.

Ghosting matrix: both X3 controllers; X4 SSD1677/UltraChip; Pro SSD1677/UltraChip;
Quick Resume to reader and home, timeout override, light/dark text, AA/images,
long sleep and repeated lock/wake cycles. Check missing/truncated saved frame
falls back safely. Test recovery chord, short wake tap, held wake release, first
page key and Pro touch. Compare banners on/off and cached/uncached books. Keep
free heap >50 KB and confirm serial capture survives both rail-cut and deep sleep.
Build with `-DLECTOR_FAST_QUICK_RESUME=0` to restore the conservative X3 sequence
if reinforcement omission ghosts. No firmware is approved to ship from host
checks alone.

## Verification record

Host commands (logs under `/tmp/lector-host-*`):

```sh
cmake -S test -B /tmp/lector-unlock-host
cmake --build /tmp/lector-unlock-host -j4
```

The first build failed because generated `lib/I18n/I18nKeys.h` did not exist.
This was a checkout prerequisite, not a failing test. Generated it using:

```sh
python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/
cmake --build /tmp/lector-unlock-host -j4
ctest --test-dir /tmp/lector-unlock-host --output-on-failure
```

Unmodified baseline: **1243/1243 passed**, 0 failed, 5.27 s.
Final sources: same build/ctest commands, **1244/1244 passed**, 0 failed, 6.40 s.
The existing X3-policy test now checks both explicit fast and conservative
branches, for both board families and banner states. One additional test checks
panel capture, two distinct base/composite submissions, freeze-at-readable and
bounded output. No generated translation files are committed.

Formatting applied and verified (exit 0):

```sh
clang-format -i lib/hal/HalDisplay.cpp lib/PerfStats/PerfStats.cpp lib/PerfStats/PerfStats.h src/WakeTiming.cpp src/WakeTiming.h src/MappedInputManager.cpp src/activities/ActivityManager.cpp src/main.cpp src/sleep/WakeFacePolicy.h test/wake_face_policy/WakeFacePolicyTest.cpp
clang-format --dry-run --Werror lib/hal/HalDisplay.cpp lib/PerfStats/PerfStats.cpp lib/PerfStats/PerfStats.h src/WakeTiming.cpp src/WakeTiming.h src/MappedInputManager.cpp src/activities/ActivityManager.cpp src/main.cpp src/sleep/WakeFacePolicy.h test/wake_face_policy/WakeFacePolicyTest.cpp
git diff --check
```

Each final firmware kit started separately with the prescribed deletion, then
ran the actual kit script (not a standalone warm `pio run`):

```sh
rm -rf .pio .cache managed_components
ESPTOOL_MACOS_ARM64=~/tools/testkit-tools/esptool-macos-arm64 bash scripts/testkit/make_testkit.sh ~/tools/testkit-www testkit
rm -rf .pio .cache managed_components
ESPTOOL_MACOS_ARM64=~/tools/testkit-tools/esptool-macos-arm64 bash scripts/testkit/make_testkit.sh ~/tools/testkit-www x4pro
```

The actual shell invocations redirected output respectively to
`/tmp/lector-kit-c3-final-source.log` and
`/tmp/lector-kit-x4pro-final-source.log` with `> ... 2>&1`.
Earlier clean C3 kit runs also succeeded while instrumentation was being refined;
they are superseded by the final-source kits. They are not the verification
basis for the final changes. No missing-object/zero-byte cache failure occurred
in the final C3 run. Source SHA-256 checks cover every touched C++/header and the
host target's CMake file, to confirm code is unchanged across the final kit builds.

C3 result: **exit 0**, kit
`/home/claude/tools/testkit-www/lector-testkit-lector-0.31.3-46171d938-dirty-20260913-112157.zip`.
The compiler reported the existing WebSockets dependency's deprecated
`NetworkClient::flush()` use. This is not a warning-free build.
Byte searches of `firmware.bin` confirmed `ms total classify=`, `panels[` and
`first_input=` are compiled into the kit. The kit's commit label is the parent
plus `dirty`, because verification happens before the requested commit.

X4 Pro result: **exit 0**, kit
`/home/claude/tools/testkit-www/lector-testkit-x4pro-lector-0.31.3-46171d938-dirty-20260913-112620.zip`.
Final compiler logs: C3 **1 warning / 0 errors**; Pro **248 warnings / 0 errors**.
Pro warnings are the WebSockets deprecation and discarded-const-qualifier
warnings in unchanged ESP-IDF sources. No final kit encountered missing-object
or zero-byte cache corruption. The same three diagnostic-string byte searches
passed for Pro. All 11 source/host-build-file SHA-256 checks passed across both
final builds. Stable kit copies are `lector-testkit-latest.zip` and
`lector-testkit-x4pro-latest.zip` in `/home/claude/tools/testkit-www`.

Hardware status: **not measured here; not cleared to ship**. In particular,
neither the exact physical floor nor a two-second button-to-readable result has
been established. The user needs to return the new wake lines and ghosting A/B
results before that claim can be made. No merge, push or PR is part of this work.

## Quick Resume removal verification

The user subsequently requested removal of Quick Resume. Both device/web controls,
the timeout override, saved-frame handling, retained-frame wake routing and the
experimental fast flag are removed. Persisted mode 6 migrates to Light without
renumbering other modes; legacy wake flags are ignored. The web API rejects hidden
retired enum values. Normal sleep-face panel sequences remain unchanged.

After removal, `cmake --build /tmp/lector-unlock-host -j4` and
`ctest --test-dir /tmp/lector-unlock-host --output-on-failure` pass **1231/1231**
(4.37 seconds). Obsolete Quick Resume policy tests were removed and migration is
covered. The SettingsPage inline script passes `node --check`; all touched C++
files pass clang-format's dry run, and `git diff --check` passes.

Both kit-script commands listed above were rerun, each preceded by
`rm -rf .pio .cache managed_components`, and both exited 0. Final logs are
`/tmp/remove-qr-c3-final.log` (1 dependency warning, 0 errors) and
`/tmp/remove-qr-pro-final.log` (248 dependency/framework warnings, 0 errors).
The stable kit filenames now contain this removal. Migration and normal lock/wake
behavior still need a device check; no two-second claim is made.
