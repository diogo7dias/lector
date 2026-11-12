---
name: verify
description: Boot the real lector firmware in QEMU (sim/server.py) and drive it like a reader does (keys, taps, swipes, reboot, sleep) to prove a change on screen, with screenshots, activity names, ink counts and the serial log as evidence. Use to verify any firmware change to UI, input, reading, settings, persistence or sleep before calling it done, or when asked to run, screenshot or demo the device.
---

# Verify lector in the simulator

The product is firmware for Xteink e-readers. Nothing runs on the host except the
simulator: `sim/server.py` boots the board's last `pio run` build in Espressif QEMU on a
fresh 16 MB flash plus a 256 MB card holding two test EPUBs
(`test/epubs/test_kerning_ligature.epub`, `test_jpeg_images.epub`), and serves it over
HTTP. Every check here is exact: activity name, frame count, panel ink. No model, no mocks.

Default board is `x4pro` (the main device, ESP32-S3, touch). Others: `x4`, `x3`,
`x3uc8279` (C3 build, button ladder, no touch). All commands go through the shipped
helper `.claude/skills/verify/bin/lv`
(C3 boards need `pio run -e default`; recipe in `features/buttons-c3.md`) (run from the repo root; `lv` below means that path).

## Launch

1. Firmware for the board, from this checkout: `pio run -e x4pro` (C3 boards: `pio run -e default`).
   First build in a fresh worktree downloads the toolchain and takes minutes; run it in
   the background. A `custom_sdkconfig` change wipes the other env's build: rebuild both.
2. QEMU: `lv` uses `$LECTOR_QEMU_DIR`, else `sim/.qemu/build`, else the main checkout's
   `sim/.qemu/build` (worktrees share it). None exists: `./sim/build-qemu.sh` (once,
   and after `sim/qemu-patches/` change).
3. `lv start x4pro`. It picks a free port, boots, and returns only when the server
   prints `lector sim on http://127.0.0.1:<port>`, which happens after the firmware
   drew Home and went idle (about 15-90 s). It prints `export LV_RUN=<dir>`; eval that
   when more than one instance may exist, otherwise `lv` uses `/tmp/lector-verify/active`.

Ready means `lv state` shows `"activity": "Home"` and `"frames"` above 0.

## Doctor

`lv doctor`, read-only. Healthy means all of: the server pid we launched is alive, our
port answers `/state`, an activity has been entered, no file under `src/` or `lib/` is
newer than `.pio/build/<env>/firmware.bin` (else you are driving a stale build), and the
serial log has no `Guru Meditation`, `abort()` or `Backtrace`. Exit 1 names the failure.
Run it first whenever anything looks off.

## Drive

- `lv state` returns `{"frames","activity","ink","asleep","controller"}`. `activity` is the
  last `Entering activity: <Name>` in the serial log (`Home`, `FileBrowser`, `EpubReader`,
  `EpubReaderMenu`, `Settings`, `Sleep`, ...). That name is the stable handle; prefer it
  over pixels.
- `lv press <key>`: x4pro keys are `up down home power` (`home` is the GT911 capacitive
  Home key). C3 boards: `back confirm left right up down power`. `power` holds 1.2 s to
  sleep, or wakes when asleep.
- `lv tap <x> <y> [ms]` and `lv swipe <x1> <y1> <x2> <y2>`: the upright panel is 480x800.
- `lv pinch <spanStart> <spanEnd>`: two fingers on a vertical line through the centre, spanStart px
  apart, move to spanEnd px apart (larger spreads, smaller pinches). Needs QEMU with patch 0012.
  Touch boards only. On x4pro a first tap arms a row (dashed outline), the second opens it.
- `lv ink <x> <y> <w> <h>` returns `{"ink","area"}` for a box. Use it to prove a value
  changed in one spot (a toggle column, a dashed outline) without a full screenshot diff.
- `lv reboot` power-cycles. Flash and card persist, so settings and reading position
  must survive it.
- `lv log [regex]` reads the firmware serial log (`LOG_INF/LOG_DBG/LOG_ERR` output).
- Each POST returns only after the device settles and answers with the new state.
  Still poll `lv state` for slow paths (book open, reboot): the activity flips before
  its first page is painted.

Coordinates for the menus live in the feature map, taken from `sim/tests/x4pro/*.e2e.ts`.
When a layout changes, those tests and the map move together.

## Evidence

- `lv shot <name>` saves `<name>.png` (the panel) and `<name>.json` (the state) under
  `$LV_RUN/evidence/`. `lv stop` adds `serial.log`. Evidence lives in
  `/tmp/lector-verify/<board>-<timestamp>/evidence/` and survives cleanup.
- Take a shot before and after the action under test, never only the end screen.
- Drive the user path: keys, taps, swipes, Power, Reboot. Never poke flash or the card
  image directly to fake a state.
- Prove side effects as a reader would see them: a setting survives `lv reboot`, a book
  reopens on the same page, `asleep` flips to true. Quote the serial log line when the
  change logs one.
- Not modelled, so not provable here: waveforms and ghosting, frontlight, battery drain,
  USB Drive, Wi-Fi (OPDS, KOSync, BookOrbit, OTA), the UC8279's own LUTs and grays.
  Say "needs device test" for those; do not claim them.

## Cleanup

`lv stop`. It kills the process group it started (server plus its QEMU, launched under
`setsid`), copies the serial log into evidence, and removes the run's `work/` scratch
(flash image, card image, frames). Never `pkill qemu`: another session or the e2e suite
may own one. Run `lv stop` after every failed attempt too.

## Regression suite

The same simulator backs the CI suites in `sim/tests/<board>/` (`cd sim && npm install &&
npm run test:e2e:x4pro`). `lv` is for proving one change mid-task; run the suite before a
PR that touches input, activities or persistence. Feature map: `features/README.md`.
