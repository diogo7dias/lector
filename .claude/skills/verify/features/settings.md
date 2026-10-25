# Settings

A reader opens Settings from Home, picks a tab, toggles a setting, and the new value is
still there after the reader is switched off and on.

## Sub-features

- `settings-open` Settings opens from Home.
- `settings-tab` a tab (Display) opens its list.
- `settings-toggle` tapping a row flips its value column.
- `settings-persist` the value survives a reboot.

## How to get to it (user POV)

- Home, Settings, then a tab along the top.

## Driving it with lv

Preconditions: baseline (default settings).

- **Open.** `lv tap 240 729` twice. `activity` becomes `Settings`.
- **Tab.** `lv tap 100 84` twice (Display).
- **Read value.** `lv ink 400 403 76 22` is the value column of "Show Favorite Badge"; note its `ink`. `lv shot before`.
- **Toggle.** `lv tap 150 413` twice. Poll `lv ink 400 403 76 22` until `ink` differs. `lv shot after`.
- **Persist.** `lv press home`, `lv reboot`, then Settings and Display again as above.
  `lv ink 400 403 76 22` equals the toggled value. `lv shot after-reboot`.

## Gotchas

- Reboot from Home, as `sim/tests/x4pro/settings.e2e.ts` does; rebooting from inside Settings is not covered by the suite.
- OFF and ON differ only in ink count in that box: compare the numbers, not a glance at the PNG.
