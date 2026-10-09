# Lector 0.41.0

## Added

- X4 Pro: upload `.ttf` / `.otf` fonts from the web page.
- TXT: a UTF-8 byte-order mark is skipped; UTF-16 files show "unsupported encoding" instead of garbled lines.

## Changed

- X4 Pro: **Touch Sleep** is now Off by default (On made touch and the Home key feel dead after a pause).
- Firmware updates and the default KOReader sync server verify their TLS certificates.
- Better battery: the web server and Calibre screens sleep with no client, the frontlight switches off before deep sleep, and the idle panel powers off again on X3 and UC8279 X4.
- Faster: X4 Pro mounts the SD card while input comes up; batched page-position reads; -O2 on layout and render.

## Fixed

- Reading position: recovered from a save cut by power loss, and kept when an EPUB is replaced outside the firmware.
- Page turns during a relayout are no longer undone; Go to paragraph waits for a chapter still being laid out; a forward turn at a partially built chapter extends it.
- An empty chapter leaves on one Back press; an empty `.txt` shows "Empty file".
- Buttons: a key tapped while another is held still acts; a pending single click is not lost.
- File browser: very long (CJK) names are listed; the cursor stays in place after delete or move.
- WebDAV: no changes to a book that is still downloading; a folder cannot be moved into itself.
- Downloads: a resumed download never keeps a stale tail; uploads of 2-4 GB keep their size.
- Crash and corruption hardening: invalid PNGs, oversized zip entries, deep inline styles, low-memory dithering, atomic book and settings writes, safer cache sweeps.
- X3: a failed battery-gauge read no longer looks like USB unplugged.
- Ported upstream: centred button hints, header title wrapping.
