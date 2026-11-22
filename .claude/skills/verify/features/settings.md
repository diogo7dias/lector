# Settings

A reader opens Settings from Home, picks a card, toggles a setting, and the new value is
still there after the reader is switched off and on.

## Sub-features

- `settings-open` Settings opens from Home.
- `settings-tab` a hub card (Display) opens its list, rows under black section headings.
- `settings-maintenance` System > Maintenance holds the cache and firmware rows; Back lands on its row in System.
- `settings-toggle` tapping a row flips its value column.
- `settings-persist` the value survives a reboot.

## How to get to it (user POV)

- Home, Settings, then one of the five hub cards (Display 101, Reader 175, Controls 249,
  System 323, File Transfer 397; x = 240).

## Driving it with lv

Preconditions: baseline (default settings).

- **Open.** `lv tap 240 729`. `activity` becomes `Settings`.
- **Card.** `lv tap 240 101` (Display).
- **Read value.** `lv ink 400 493 76 22` is the value column of "Show Wallpaper Position"; note its `ink`. `lv shot before`.
- **Toggle.** `lv tap 150 503`. Poll `lv ink 400 493 76 22` until `ink` differs. `lv shot after`.
- **Persist.** `lv press home`, `lv reboot`, then Settings and Display again as above.
- **Maintenance.** In System (`lv tap 240 323`), `lv tap 420 781` (Page Down), then `lv tap 150 733`.
  `lv ink 400 493 76 22` equals the toggled value. `lv shot after-reboot`.

## Gotchas

- Reboot from Home, as `sim/tests/x4pro/settings.e2e.ts` does; rebooting from inside Settings is not covered by the suite.
- OFF and ON differ only in ink count in that box: compare the numbers, not a glance at the PNG.
