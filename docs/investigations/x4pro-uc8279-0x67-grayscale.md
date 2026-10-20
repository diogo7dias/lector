# X4 Pro UC8279 LUT_VER 0x67: grayscale feasibility investigation

## Verdict

**Do not enable grayscale on LUT_VER 0x67 without hardware. No code change.**

Investigation only, 2026-10-20, branch `fix/pxc-0x67-gray`. No 0x67 device was available; nothing was pushed. The evidence closes the two cheapest hypotheses and leaves one expensive one.

## The problem

On X4 Pro units whose UC8279 probe reads LUT_VER 0x67, sleep wallpapers (`.pxc`) draw pure black and white. The chain:

- `src/activities/boot_sleep/SleepFacePaint.h:131` — `renderer.displayGrayBuffer(/*fullTone=*/true)`.
- `lib/GfxRenderer/GfxRenderer.cpp:2228` → `lib/hal/HalDisplay.cpp:435-453` — lector's split-gray override picks the bank by variant: `v == 0x02 || v == 0x03 ? kSplitGray02 : kSplitGray68`. A 0x67 unit falls into the 0x68 arm, but the result is discarded (next point), so the wallpaper plays through the base path instead.
- freeink-sdk `libs/display/FreeInkDisplay/src/driver/Uc8279X4Driver.cpp` — `otpOnly()` (lines 96-101) is `BoardConfig::ACTIVE.displayControllerVariant == 0x67`, and it gates every grayscale plane: `copyGrayscaleLsb` (433), `copyGrayscaleMsb` (447), `displayGray` (465), and `displayGrayscaleBase`'s choose-fallback branch (532). The base frame takes a plain B/W `display()`.

## Why 0x67 is OTP-only

The pinning evidence, in order of authority:

1. **Stock X4 Pro 260917 build**, panel LUT registry at `0x3c1adbd4` plus hardcoded bank selects in its UC8279 refresh (cited at `Uc8279X4Driver.cpp:87-91`): it knows 0x02/0x03 (QY bank), 0x68/0x69 (ZHX bank), and — per X4C V7.1.21 — BOE 4.28 D/E 0x41/0x42 (ZHX bank). Anything unknown falls back to the ZHX bytes. **0x67 is the one explicitly excluded from that fallback**: a registered, shipping id that stock deliberately makes refresh from OTP (B/W) only, while even the *reserved* 0x69 gets the ZHX fallback.
2. Upstream Free-Ink main commit `1ff1e925cfe7741d593dd2e62e84ee947fccd7e9` (2026-09-17, "Add support for UC8279 LUT_VER 0x03 and 0x67 variants"): "The 0x67 variant reports grayscale as unsupported to prevent using incompatible waveforms." — stock's registry ships no external-LUT tables for it.
3. Lector's port: freeink-sdk `7a8c899` (2026-10-05) and lector `40828a00a`, both mirroring upstream's reasoning; lector PR #204.

**Is it missing bytes, or was a gray attempt tried and broken on glass?** The record cannot distinguish the two. No commit, doc, note, or issue in either repo (searched: `git log -i --grep=0x67` in lector and freeink-sdk, `-p` history of the three files, Free-Ink org issue search) records any grayscale attempt on 0x67. What the record does establish is intent in both directions: stock's author had the ZHX bank in hand, handed it as fallback to every unregistered *and* reserved id, and still refused it for 0x67 specifically. That asymmetry is evidence against "the ZHX waveform is safe on 0x67 and it was simply never wired," and equally it is not evidence of a documented failed attempt.

## Why the "share the nearest bank with 0x68" premise fails

- LUT_VER names the glass's OTP waveform set; nothing in the record ties 0x67's panel to the ZHX (0x68) or QY (0x02) bank lineage.
- The bank-swap risk is not cosmetic: the AA path leaves charge and participates in the anti-ghost budget (`Uc8279X4Driver.cpp` copyGrayscale*/displayGray comments; lector `HalDisplay.cpp:422-427`). A wrong waveform lands exactly in the stuck-ghosting territory PR #343 fixes, and B/W pages on those panels can only recover through the FULL-refresh budget.
- It cannot be verified from here: no 0x67 unit exists in the test loop (`sim/` covers x4pro's SSD1677 default profile only; the QEMU boards do not model UC8279 variants).

## What a device-verified test looks like (when a 0x67 unit is available)

The machinery already exists and needs no new waveform bytes:

- The driver's caller-override hook takes external AA bytes per refresh (`7a8c899`'s predecessor `b1a232b`, used by lector under `LECTOR_X4PRO_SPLIT_MIDS`).
- Lector already selects `kSplitGray68` for 0x67 at `HalDisplay.cpp:447`; the SDK then discards it.

One safe experiment, gated behind a build flag defaulting OFF:

1. read the boot log and confirm the panel: freeink-sdk logs `[XTDET] promoted SSD1677 -> UC8279 800x480 (LUT_VER=67, OTP-only)` (`XteinkDetect.cpp:430`), and lector logs `HW: X4 Pro display controller=6` (= UC8279, `lib/hal/HalGPIO.cpp:166`);
2. read OTP through the driver diagnostic path to find the panel's own AA bank in its waveform set;
3. relax the four 0x67 gates in `Uc8279X4Driver.cpp` (lines 433, 447, 465, 532) under the flag, and feed the ZHX-68-derived split LUT;
4. test: setting a `.pxc` wallpaper → four tones, clean page turn after the wallpaper, full refresh, sleep/wake;
5. collect the result either way (working → flip the flag default ON with the evidence at hand; broken → record the broken attempt next to registry entry 0x67).

## Sources

- freeink-sdk `ea414a5`: `libs/display/FreeInkDisplay/src/driver/Uc8279X4Driver.cpp:87-101` (variant-registry comment and `otpOnly()`), `:433`, `:447`, `:465`, `:532` (the four gates).
- freeink-sdk commits `7a8c899` (0x03/0x67 port), `b1a232b` (waveform override hook).
- freeink-sdk `libs/hardware/XteinkDetect/src/XteinkDetect.cpp:415-431` and `test/host/test_display_probe.cpp:138-146` (probe promotion, `LUT_VER=67 … OTP-only` log).
- lector commits `40828a00a` (SDK bump), `0125d4344`/`6b465e86e` (wallpaper tone work on the same family).
- Upstream Free-Ink `1ff1e925cfe7741d593dd2e62e84ee947fccd7e9` patch text.
