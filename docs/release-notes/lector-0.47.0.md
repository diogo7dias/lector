# Lector 0.47.0

## New

- **Reading toggles in the light panel**: Align Left, Justify and Paragraph Spacing sit next to the light controls while you read. They apply per book in EPUB; alignment is global in TXT.
- **TXT status bar** now shows Pages This Session, Pages to Paragraph and Book time left.
- **Crow logo** on the boot screen and on the plain sleep face.

## Changed

- Changing a reading setting from the light panel goes straight back to the page, without an Indexing banner. Toggling a setting back reuses the chapter's previous layout instead of rebuilding it.
- The busy and message banner is white with a black rule, so it no longer ghosts onto the sleep wallpaper.
- Firmware files are no longer signed: `firmware.bin` is a plain image.

## Fixed

- Pages to Paragraph no longer disappears from the status bar while a long chapter is still building.
- Auto Sync uploads silently when BookOrbit holds no position for a book, instead of asking Upload or Download.
