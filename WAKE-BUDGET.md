# X4 Pro wake budget (arch/wake-fast)

Ranked by ms still on the path at `5759e8f80`. Provenance: **M** = device-measured stamp
(X4 Pro UC8279 unit, lector 0.28/0.29 kit logs in ~/.claude/uploads, code for that stage
unchanged since), **S** = source constant/comment, **E** = estimate from code, **?** = unknown.

| # | Stage | file:line | ms | Prov |
|---|---|---|---:|---|
| 1 | perf + trace session-file probe (`exists()` per old session, up to 200 in /perf + 50 in /) | src/PerfLogSink.cpp:50, src/util/DebugTrace.cpp:35 | 0 to ~2200 (disp stage grew 281 at w40 to 2335 at w247) | M (growth), E (attribution: only O(files) work in that stage) |
| 2 | first reader paint, drive-all DU FAST | src/main.cpp:707, Uc8279X4Driver.cpp:288 | ~443-505 | S |
| 3 | gpio.begin: UC81xx bus probe (~117) + GT911 power/reset (~170) | lib/hal/HalGPIO.cpp:144, InputManager.cpp beginGt911 | 294 | M (sum), S (split from delays) |
| 4 | SDMMC mount incl. OEM power cycle | lib/hal/HalStorage.cpp:27 | 243 | M |
| 5 | display begin + built-in + SD fonts | src/main.cpp:682-722 | ~141 | M (flash-boot log 3120-3261) |
| 6 | config load | src/main.cpp:587-613 | 62-82 | M |
| 7 | framework before setup (deep-sleep wake, no serial delay) | src/main.cpp:470 | 42 | M |
| 8 | route + font + book | src/main.cpp:915-960 | 17-105 | M |
| - | ROM/bootloader before millis() | - | ? | ? |
| - | clearing pass | src/main.cpp:702-719 | 0 (DriveAll default since 0.31.5; was 1370 M) | M/S |
| - | recovery settle | src/WakeFacePolicy.h inputSettleMs | 0 (20 ms window already elapsed; was 124 M) | S |

Floor: DU waveform ~443-505 ms (S). Nothing in code goes below that.
