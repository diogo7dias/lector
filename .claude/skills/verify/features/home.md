# Home and navigation

Home is the first screen after boot: a list of rows (Browse Files, Settings, ...). On the
X4 Pro a first tap arms a row with a dashed outline, a second tap opens it, and the
capacitive Home key comes back from anywhere.

## Sub-features

- `home-boot` boot lands on Home.
- `home-arm` the first tap arms a row and draws a dashed outline, without opening it.
- `home-open` the second tap on the armed row opens it.
- `home-key` the Home key returns to Home.
- `home-fast-tap` a tap that lifts before the next touch poll still lands.

## How to get to it (user POV)

- Switch the reader on.
- Press the Home key from any screen.

## Driving it with lv

Preconditions: baseline (fresh `lv start x4pro`).

- **Boot.** Run `lv state`. `activity` is `Home`, `frames` above 0.
- **Arm.** Tap Browse Files once: `lv tap 240 517`. `activity` stays `Home`; each edge strip of
  the row is partly inked: `lv ink 20 495 440 2` (top), `lv ink 20 538 440 2` (bottom),
  `lv ink 20 495 2 45` (left), `lv ink 458 495 2 45` (right), each `ink/area` between 0.3 and 0.9.
- **Open.** `lv tap 240 517` again. `activity` becomes `FileBrowser`.
- **Home key.** `lv press home`. `activity` becomes `Home`.
- **Fast tap.** `lv tap 240 729 0` twice (Settings, zero hold). `activity` becomes `Settings`.
- **Proof.** `lv shot home-armed` after the first tap, `lv shot filebrowser` after the second.

## Gotchas

- Taps are on the upright panel, 480x800, not the page's scaled image.
- A tap on a different row moves the arm instead of opening anything.
- C3 boards (`x4`, `x3`) have no touch: `lv tap` returns 400. Use `lv press down` / `confirm`.
