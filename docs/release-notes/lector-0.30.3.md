## Fixed
- **X4 Pro: a tap along the bottom of a book page no longer leaves the book.** The painted hint band outlived the screen that drew it. A reader page paints no hints, so the home/menu band stayed live and the bottom-left of the page was Back.
- **Dictionary word-select Left/Right/Up/Down hints work on touch.** Tap queries answered inside the band and ate the press before the buttons were read. Those queries now refuse the band.
- **A confirmation answered by tap no longer re-fires the list row it popped back to.** The synthetic release a hint-band tap owes is dropped on pop-back the same way a held key's release is.
- **Customise Status Bar Position picker: Back stays on the screen, Select closes once.** The option popup closed on press and leaked the release to the host. It now swallows that release for every host.

## Notes
- Same two images as 0.30.2: `firmware.bin` for X3/X4, `firmware-x4pro.bin` for X4 Pro. An X4 Pro already on 0.30.0–0.30.2 can OTA this.
