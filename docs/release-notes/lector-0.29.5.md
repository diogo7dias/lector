## Changed
- **Every screen now draws through the same theme.** The last screens that painted themselves — bookmarks, footnotes, quotes, chapter lists, the file browser, Home, the reader menu, both settings screens, the font pickers, the button-remap wizard, the QR and image viewers and the confirmation screens — moved onto the shared bases. Type sizes, row heights, headers, button hints and selection all come from one place, so they can no longer drift apart from screen to screen.
- **Home is one list.** Pressing Down past the last book walks straight into Browse, OPDS, Transfer and Settings rather than stopping at the end of the books.
- **Long quotes wrap.** A quote longer than a row is shown in full over several lines instead of being cut, with its chapter against the right edge.
- **The X4 Pro's touch controls stay on the X4 Pro.** The draggable slider capsule, its step buttons and the settings cells drawn as buttons only appear on the one device with a touch panel.
- **The X3 and the X4 keep their old settings list.** Settings and Text Settings are one column of full-width rows again — name on the left, value on the right — the way they were through 0.28.0. The category hub stays on every device. Numbers are set with a plain bar, and the clock-offset fields are outlined with the active one greyed, instead of the touch shapes.

## Fixed
- **A tap on a button hint now works everywhere.** The hint band synthesised a press without the release that follows it, so any screen that tells a press from a hold — the file browser's **Open**, among others — saw the press and waited forever for the release. Tapping **Open** on a highlighted folder did nothing.
- **The on-screen keyboard answers to touch.** Its keys were drawn with hit areas that nothing was listening to, so on the X4 Pro only the physical buttons typed. Tapping a key now types it, and holding one types its alternate character.
- **A QR screen repaints cleanly.** It now takes a full refresh, so no trace of the previous page shows through the code.
- **The slider band no longer clips its title.**
