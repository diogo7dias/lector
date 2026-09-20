# lector 0.31.27

## Removed

- **Reading statistics.** The whole feature is gone: the stats dashboard, the
  per-book and global session tracking, the SD-card stats files, and the two
  settings that controlled it (`Track Reading Stats`, `Reading Idle Limit`).
  2,281 lines removed; flash freed on both builds.

  Two settings options that pointed at it are **retired, not renumbered**, so no
  existing install silently changes behaviour:
  - Quick action `Stats Dashboard` → migrates to **Disabled**
  - Long-press action `Reading Stats` → migrates to **Disabled**

  Stats files already written to an SD card are left untouched. The firmware
  simply stops reading and writing them; delete them yourself if you want the
  space back.

## Fixed

- **Two-tap confirmation on settings rows (touch boards, X4 Pro).** A first tap
  on a settings row painted it as a filled selected band — identical to a
  completed activation — because the row style dropped the themed armed look and
  the arming tap marked the row selected. An armed row now wears the 1px outline
  the design calls for, distinct from and weaker than the selected fill, so the
  first tap reads as "confirm this" rather than "done".

  Keys-only boards (X3, X4) are unaffected: two-tap is never enabled there.
