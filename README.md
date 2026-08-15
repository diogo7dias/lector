# lector

E-reader firmware for the Xteink X3 and X4.

**Install:** [browser flasher](https://diogo7dias.github.io/lector-xteink-firmware/#flash), or download `firmware.bin` from [Releases](https://github.com/diogo7dias/lector/releases), copy it to the SD card, and use Settings → SD Card Firmware Update.

### Reading

- Per-book reader settings: fonts, size, spacing, margins, indent, all saved per book.
- Reading Themes: save a set of reader looks and apply them per book.
- Steal Look: copy another book's settings.
- Reading position is held by paragraph, so a font change puts you back on the same paragraph.
- Paragraph numbers: off, per chapter, or whole book, in two sizes.
- Go to Paragraph: jump straight to a number.
- Granular paragraph spacing and first-line indent.
- Paperback Look: heavier ink for reader text.
- Bionic Reading and guide dots.
- Portuguese hyphenation.
- Vollkorn as the built-in reading font, Cozette for the UI.

### Quotes

- Grab Quote: pick a word range with the buttons and save it to a sidecar file.
- Quotes may run across page boundaries.
- Saved quotes keep a thin underline in the book.
- View Quotes screen, one page per quote.

### Home and library

- Home shows your in-progress books with a progress badge, full wrapped titles, and author.
- Finished books are filed away automatically.
- Search the current folder from the file browser.
- Random file browser order, and Open Random Book on Boot.
- Book Info screen with the book description.
- In-book Delete Book, and remove from Recents from inside the book.
- Clean Up Storage: sweeps orphaned cache only.

### Sleep screens and wallpapers

- Browse, favourite, pause and triage sleep wallpapers on the device.
- Hold the current wallpaper instead of rotating it.
- `.pxc` wallpapers accepted.
- Stats Dashboard and Freeze sleep faces.
- [Wallpaper Converter](https://diogo7dias.github.io/lector-xteink-firmware/): turn any image into an X3/X4 wallpaper in your browser, or pick one from the gallery there.

### Elsewhere

- Reading statistics and a Reading Stats screen.
- Tabbed in-book reader menu, sectioned, with Menu Hold to open it.
- Status bar customised per item, and per book on or off.
- Firmware version on the home header.

### Removed on purpose

- Tilt page turn (the gyro) is gone entirely.
- The Recent Books screen is gone; the home list replaces it.

---

lector is a personal fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), and is kept in sync with the upstream repository: upstream fixes and features are pulled in regularly.
