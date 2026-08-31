# Next up

Session handoff, 2026-08-31. Delete or rewrite this file when the queue below is empty.

## Where things stand

Branch `claude/busy-feedback`, commit `adcd398fc` "Show a busy strip while a screen change is being built".
**Committed, not pushed, not merged.**

The change arms one `BusyBanner` at the single transition point in `ActivityManager::loop()`, so any slow
`onExit()`, constructor or `onEnter()` gets a top strip once it drags past the banner's 400 ms delay. Labels come
from the pure, host-tested `src/activities/ActivityBusyLabel.h`. `busy::tick()` was added to the three blocking
loops that lacked it: recent-book loading in `HomeActivity`, font sizing in `InstalledFontsActivity`, quote
parsing in `QuotesViewerActivity`.

Verified before the kit was cut: 1128 host tests pass (baseline 1123, plus 5 new). `pio run -e default` and
`pio run -e x4pro` both succeed. clang-format applied.

## 1. Run kit 44 on the X4 Pro (blocking, needs the device)

Prerelease `x4pro-testkit-2026-08-31a` on `diogo7dias/lector-xteink-firmware`, titled "X4 Pro test kit 2026-08-31a".

One paste-and-run command:

```
cd ~/Downloads && curl -fL -o kit44.zip https://github.com/diogo7dias/lector-xteink-firmware/releases/download/x4pro-testkit-2026-08-31a/lector-testkit-x4pro-lector-0.29.5-adcd398fc-20260831-123015.zip && rm -rf kit44 && unzip -q kit44.zip -d kit44 && bash "$(find kit44 -name run.command | head -1)"
```

The earlier command failed with `zsh: no matches found: kit44/*/run.command` because the zip's directory depth is
not what the glob assumed. The `find` above sidesteps that; keep it in future kit commands.

Checks 3, 8 and 9 are the ones that would expose the banner firing too eagerly, so read those results first.

## 2. Test kits still owed

- Kit 43 on X3 or X4, 8 checks. The FreeInkUI sync work.
- Kits 40 and 41 on the X4 Pro, 28 and 8 checks.

## 3. Option B, deferred on purpose

The banner only appears if something calls `busy::tick()`. Screen changes are covered; the network paths are not.
Widening coverage means adding `busy::tick()` or `busy::tickNow()` to: OPDS fetch and feed parse, file-transfer
Wi-Fi associate and web-server start, OTA manifest fetch and download, Wi-Fi scan, Calibre, KOReader sync and
Nearby transfer. These are the waits that run into seconds, so they are worth the most. Deferred until the
mechanism is proven on device by kit 44.

Honest remaining limit either way: a path with no loop at all, and a true lockup, still show nothing. The touch
and button paths themselves are not instrumented; only screen changes are.

## 4. Worth a separate branch: no X4 Pro release image

`.github/workflows/release.yml` builds only the `gh_release` environment, which is ESP32-C3. It attaches
`firmware.bin`, `bootloader.bin`, `partitions.bin` and `firmware.elf`. The `x4pro` environment
(`platformio.ini:274`, board `esp32-s3-devkitc1-n16r8`) is never built, so no `firmware-x4pro.bin` ships and an
X4 Pro cannot take a release over the air or from SD. Pre-existing; `0.29.5` did not introduce it.

Related, already answered from code: the X3/X4 split is a runtime one via `mappedInput.hasTouch()`, and one C3
binary correctly serves both. Only the X4 Pro needs its own image.

## 5. Standing open work, unchanged

- X4 Pro fast-waveform retune: black depth needs a measured pick, not a derived one.
- Wake behaviour from `#3191`.
- Upstream catchup PRs `#3247`, `#3252`, `#3245`.
- Revoke App Store Connect key `GACC2P4ZW5`, exposed in a transcript.
