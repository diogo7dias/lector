## Added
- **Reading Stats can be bound to a button.** Settings > Controls > Buttons, any key, click / double / hold, in book or outside. In a book it opens this book's stats and pauses the clock; elsewhere it opens the most recent book still on the card, same as Home Back.

## Fixed
- **Check for Updates no longer offers upstream CrossPoint as a newer Lector.** A dropped fetch against this fork's GitHub endpoint used to fall through to `crosspoint-reader`, whose `v1.x` tag parses newer than `lector 0.x`. Upstream is now only asked by Install Other Firmware, the unlocker path.
- **`lector-flash-diagnostics.txt` keeps every record.** Each write used to truncate the file, so the boot record wiped the failure it was meant to explain. Writes now append.

## Notes
- Same two images as 0.30.3: `firmware.bin` for X3/X4, `firmware-x4pro.bin` for X4 Pro. An X4 Pro already on 0.30.0–0.30.3 can OTA this.
