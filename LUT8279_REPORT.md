The LUT Lab now targets the UC8279 X4-class AA waveform used by the reported
X4 Pro. It retains wallpaper browsing, pin/unpin, sleep persistence, control/reset,
and ordinary release availability. No shipping grayscale default is replaced.

**Changes, file by file**

| File | Change |
| --- | --- |
| `src/lut/LutLabLuts.h` | Replaces the SSD1677 voltage/timing probes with eight flash-resident UC8279 AA banks; each contains five contiguous 49-byte payloads. Documents the single tuning axis. |
| `lib/hal/HalDisplay.cpp` | Gates the lab on non-X3 UC8279 devices, matching the SDK's X4 route; exposes the native control frame count for accurate labels. Retains the scoped override forwarding. |
| `lib/hal/HalDisplay.h` | Declares the control-frame query and documents the 245-byte override format. |
| `src/lut/LutLabActivity.cpp` | Shows the actual AA byte-2 values for the two table groups, including the native stock value for variant 0. Reuses all existing list actions. |
| `src/lut/LutLabState.h` | Adds a waveform identity to persisted selections. Old SSD1677 indices reset to control instead of silently becoming different experiments; wallpaper and pin survive. |
| `lib/I18n/translations/english.yaml` | Updates translated timing/byte labels and removes obsolete voltage labels. Other languages retain the existing English fallback. |
| `freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8279X4Driver.cpp` | Consumes the override in the real AA upload loop. Null still selects native Aa02/Aa68. Makes the stock waveform arrays `static constexpr`; their bytes do not change. |
| `freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8279X4Driver.h` | Documents the override's byte layout and synchronous lifetime. |
| `freeink-sdk` gitlink | Records local SDK commit `326e3abde` containing that driver change. |
| `test/lut_lab/CMakeLists.txt` | Compiles the real UC8279 X4 driver into the existing host test target. |
| `test/lut_lab/LutLabTest.cpp` | Records driver bus writes, checks all stock/override bytes and ordering, checks single-axis variants, round-trip state, corruption defaults, and old-state migration. |
| `test/lut_lab/stubs/Arduino.h` | Minimal host clock/GPIO definitions needed to compile the driver. |
| `test/lut_lab/stubs/SPI.h` | Host declaration of the SPI settings member; no hardware traffic. |
| `test/lut_lab/stubs/BoardConfig.h` | Shared host board inputs, including mutable LUT_VER. |
| `test/lut_lab/stubs/esp_heap_caps.h` | Host allocator shim for linking the unchanged driver lifecycle. |
| `USER_GUIDE.md` | Updates supported devices, exact variant meanings, screen labels, reset behavior, and device instructions. |
| `LUT8279_REPORT.md` | This report. |

**Variants**

All entries below are hexadecimal data byte **index 2**, excluding the command
byte. Every other byte remains exactly Aa02. There are no voltage, PLL, repeat,
polarity, or preBW changes.

| Index | 0x20 VCOM | 0x21 WW | 0x22 BW | 0x23 WB | 0x24 BB | Change and reason |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | 02 | 02 | 82 | 82 | 02 | Untouched native control on the user's LUT_VER=02 panel. |
| 1 | 01 | 01 | 81 | 81 | 01 | One frame below Aa02, to establish the effect of reducing drive. |
| 2 | 03 | 03 | 83 | 83 | 03 | One frame above Aa02; byte-exact match to vendor Aa68. |
| 3 | 04 | 04 | 84 | 84 | 04 | Next single-frame increase. |
| 4 | 05 | 05 | 85 | 85 | 05 | Next single-frame increase. |
| 5 | 06 | 06 | 86 | 86 | 06 | Next single-frame increase. |
| 6 | 07 | 07 | 87 | 87 | 07 | Next single-frame increase. |
| 7 | 08 | 08 | 88 | 88 | 08 | Final probe; cap the sweep at four times Aa02's frame count. |

Variant 0 passes null, so LUT_VER=68/69 (and the SDK's unknown-version fallback)
keep their native `03/03/83/83/03` control and display those bytes. Probes 1–7
have the same explicit bytes on every supported unit. On a 68 unit, variant 2
therefore duplicates its control.

The five frame counts step **together**, preserving the BW/WB 0x80 flags. This
follows the vendor's own Aa02→Aa68 difference and tests one duration parameter;
separating the dark-gray tables would also introduce a phase-duration mismatch.
The 42-byte previous→current preBW waveform is a separate, deferred tuning axis.
It remains untouched so its effect cannot be confused with the AA sweep.

This version supports the UC8279 X4-class path, including X4 Pro, only. Keeping
SSD1677 too would require two payload formats, variant sets, labels, and persisted
selection identities around the same raw-pointer SDK API. The chosen scope keeps
that format unambiguous for the reported hardware. X3, including UC8279d, is
explicitly excluded from the lab; its driver and normal rendering are unchanged.
LUT selection adds no heap allocation or framebuffer copy. The waveform identity
uses the existing state JSON document; it adds one small persisted field.

**Evidence that the selected bytes reach the panel path**

The existing call chain was traced in source:

1. `SleepActivity::onEnter()` validates the saved variant, calls `setSleepLut()`
   before rendering, and clears it afterward. Control passes null. Sleep also
   clears display inversion, so the SDK's inverted-mode AA early return does not
   suppress this render.
2. Both BMP and PXC grayscale renderers call `GfxRenderer::displayGrayBuffer()`.
3. That calls `HalDisplay::displayGrayBuffer()`, which forwards `sleepLut` to
   `FreeInkDisplay::displayGrayBuffer()` and then the selected driver's `displayGray()`.
4. The changed `Uc8279X4Driver::displayGray()` now sends the selected buffer in
   five 49-byte writes, under commands 0x20 through 0x24, before DRF (0x12).
5. Production `EpdBus::data(pointer, length)` calls `SPI.writeBytes(pointer, length)`
   inside the data transaction. No second LUT selection occurs after the override.

The host test compiles the **actual SDK driver**, substituting only the bus and
hardware dependencies. Across all eight variants and LUT_VER 02/68/69/unknown,
it checks every one of the 245 emitted bytes, command order, payload lengths,
LUT-before-DRF ordering, and native stock before and after each override. Thus it
also compares the lab's control against the actual stock arrays, including zeros.
A temporary driver copy with the old ignored-override behavior restored fails
this test. This proves software routing at the bus boundary, not electrical SPI
traffic or a physical panel result.

**Verification**

- Host configure/build/CTest: **1,387/1,387 passed**.
- Mutation check: the original ignored-override behavior is rejected.
- SDK `jev gate`: accepted.
- `pio run -e gh_release`: passed. Final application: 4,081,365 bytes flash;
  63,956 bytes static RAM (these are linker totals, not runtime free-heap measurements).
- `pio run -e x4pro`: passed. Final application: 3,973,378 bytes flash;
  74,268 bytes static RAM.
- Linked `gh_release/firmware.elf` and `x4pro/firmware.elf`: both emitted `lutlab::VARIANTS` copies
  (1,960 bytes each) and all three UC8279 waveform arrays are in `.flash.rodata`.
- Changed parent C++ files pass `clang-format --dry-run --Werror`; both diffs pass
  `git diff --check`.
- `pio run -e default`: passed. Final application: 4,126,695 bytes flash;
  63,972 bytes static RAM.
- `pio check -e gh_release --src-filters '+<src/lut/> +<lib/hal/HalDisplay.cpp> +<freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8279X4Driver.cpp>'`:
  passed, no defects found in this focused static check.
- Parent `jev gate`: accepted; result recorded in `/tmp/l8-parent-gate.json`.

The builds emit warnings in unchanged framework/dependency code (including
const-qualifier warnings and the WebSockets deprecated `flush()` call); they are
not warning-free builds. Host compilation also reports warnings in unchanged code.

Build and test logs for this session are in `/tmp/l8-host-config.log`,
`/tmp/l8-host-build.log`, `/tmp/l8-host-test.log`, `/tmp/l8-mutation.log`,
`/tmp/l8-gh_release.log`, `/tmp/l8-x4pro.log`, `/tmp/l8-default.log`, and
`/tmp/l8-static-check.log`. Local OTA application binaries are
`.pio/build/gh_release/firmware.bin` (X3/X4) and
`.pio/build/x4pro/firmware.bin` (X4 Pro); neither has been published.

**Instructions for the person holding the device**

After installing the eventual normal release from Settings, open **Home → LUT Lab
(temporary)**. The header shows `Variant N`, the AA phase bytes, pin status, and
the selected wallpaper path. For example, variant 4 shows
`AA[2] 20/21/24=0x05; 22/23=0x85`.

Use **Up/Down** to highlight a row and **Confirm** to activate it, or tap the row.
Choose **Next wallpaper** / **Previous wallpaper** to browse BMP/PXC files in
`/sleep`, `/.sleep`, and the root sleep files. Choose **Pin selected wallpaper**
to hold the image constant. Start at **Variant 0 - Control**, use the usual
**Power** lock gesture, inspect the grays, then wake and reopen the lab. Use
**Next variant** / **Previous variant** and repeat. Both lists wrap. Choices
survive sleep; **Back** saves and returns Home.

**Unpin wallpaper** restores normal wallpaper selection while retaining the
chosen waveform. **Reset: control + unpin** restores both stock waveform and
normal image selection. An unreadable pin shows an error rather than substituting
another image; a save failure keeps the lab open. Report the variant number,
displayed bytes, and wallpaper filename. Use an image with midtones: a pure
black-and-white image cannot show this difference.

**Unverified and delivery boundaries**

No physical device, OTA installation, visual contrast, ghosting, refresh duration,
or long-term panel behavior was tested. Longer phase counts are experiments, not
a calibrated recommendation; they may lengthen refreshes. No winner is shipped.

The lab has no `LECTOR_LOCK_LAB` or other experimental build guard. The existing
normal release workflow builds both `gh_release` (X3/X4) and `x4pro` (X4 Pro),
which becomes `firmware-x4pro.bin`; the lab needs no special release workflow.
No USB flashing or serial-log delivery is needed. No push, tag, or release is
performed. The SDK change and parent gitlink are local commits; a future release
must publish the SDK commit too so a fresh checkout can resolve it.
