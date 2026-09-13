# Lector 0.31.4

## Reading and sleep

- Full text pages balance leftover vertical space above and below the text inside the reading viewport, without changing line spacing. EPUB positions are cached; no extra screen refresh or heap allocation is needed. Short pages and EPUB pages with images, rules or ruby keep their layout. Line boxes balance within one pixel; letter shapes can still look slightly uneven.
- Quick Resume and its timeout override have been removed from device and web settings. Existing Quick Resume selections migrate to Light.
- Release logs now report wake stages and panel submission times so real device wake latency can be measured. A two-second wake is not yet verified.

EPUB section caches rebuild automatically once after this update. Reading progress is retained.

Validated with 1,232 passing host tests and clean C3 and X4 Pro test-kit builds. Device checks remain: full-page alignment at different line spacings, Quick Resume migration, and normal lock/wake ghosting and timing.

The X3 and X4 use `firmware.bin`. The X4 Pro uses `firmware-x4pro.bin`.
