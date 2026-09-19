# lector 0.31.20

Font downloads work on the X3 and X4. Longer battery life on both boards.

## Manage Fonts on the X3 / X4 (ESP32-C3)

Downloading a font family on a C3 device failed at one of two places: the
catalogue would not load, or a family could be listed and then refused with a
memory error. Both are fixed. A nine-file family (7.6 MB) now downloads and
installs, each file CRC-verified.

What was wrong, in order of discovery:

- **The lent framebuffer was two thirds wasted.** Two 17408-byte slots were
  reserved for wolfSSL's record buffers, but only one is live outside a brief
  window, so ~17 KB sat idle while small allocations fell back to the system
  heap and failed. The loan is now a single pool; nothing spills.
- **The manifest fetch ran on the system heap alone.** It is now covered by the
  same framebuffer loan as the file downloads.
- **The admission gate measured the wrong memory.** It tested free system heap
  against a floor calibrated when wolfSSL allocated from that heap. With the
  loan active those bytes come from the pool, so the gate refused transfers
  with 51 KB of pool free and untouched. It now tests pool capacity and
  contiguity as well, with floors derived from measured device transfers.
- **Every TLS read allocated a duplicate receive buffer.** `SecureClient`
  received into its own buffer and copied into wolfSSL's; it now receives
  directly into wolfSSL's scratch-backed record. 1436 bytes per read returned
  to the system heap.
- **The font list kept its backing arrays during transfers.** `clear()` frees
  the strings but keeps the vectors' storage, pinning 3300 bytes through every
  download. They are released and rebuilt afterwards.

The same gate guards the OTA update check, so a C3 that could not check for
updates for lack of memory should now succeed.

Diagnostics are on by default: the failure screen names the actual error
(out of memory, no reply, HTTP status) instead of one generic message, and the
log reports pool low-water, heap low-water during the loan, and spill counts.

## Battery

Ported from Flowe 0.7's power work, adapted to this firmware:

- ESP-IDF power management with automatic light sleep, floor 80 MHz on both
  targets so the APB clock stays constant and the e-ink SPI transfers are
  unaffected.
- The idle wait now yields in 10 ms slices instead of 1 ms, which is what
  lets tickless idle actually engage.
- USB CDC keeps the device out of sleep while a serial monitor is attached.

Measured savings are not yet reported from a device; the mechanism is standard
ESP-IDF automatic light sleep.

## Transfers

Interrupted downloads resume instead of restarting. Files land as `.part` and
are renamed only after the size and CRC check, so a failed transfer cannot
leave a half-written font behind. A family that arrives incomplete keeps the
styles that did land and lists the rest as still missing.

## Fixes

- Grayscale waveform override respected on UC8279 X4 panels.
- A null pointer could reach the wolfSSL scratch allocator.
- Font discovery ran twice per failed attempt, rescanning every directory.
- Catalogue entries not yet installed were logged as file-open errors; they are
  now reported as available updates, which is what they are.
