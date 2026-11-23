# Lector 0.59.0

Books lay out again once on first open: the layout cache format changed (section v93).

## New

- **Touch and navigation rethink (X4 Pro).** One tap opens a row. Only actions that cannot be undone ask for a second tap. No row is highlighted until you press a side key. Back returns to the screen you came from, and a swipe up from the bottom edge goes Home.
- **Settings in five cards** with a summary line each, Maintenance under System, and a strip under the Text Settings preview to step or drag a number while the page reflows.
- **Library:** every book on the card by title and author, with search.
- **Quick Panel on the X3 and X4:** a Power double click opens it, the keys walk it.
- **Book Info** in the file browser's File actions, with cover and description. **All Bookmarks** in Settings > Reader.
- **Quotes:** hold a word and drag to select a passage, look it up or save it. Saved quotes go to `My Clippings.txt` and to BookOrbit as highlights.
- **Ruled Lines** under each text row, in three strengths.
- **GIF images and covers** in books.
- **Built-in ChareInk** at 12, 14 and 16 pt when no SD font is selected.
- **Sleep:** nine sleep faces, Quick Resume on timeout, Wake on USB Plug-In, and a finished-book page.
- **OPDS:** custom headers and OAuth tokens. Errors tell a failing server and a redirect from a wrong password.
- **Time zones:** a full time zone table and a Daylight Saving override.
- **Reader menu:** toolbar style, with Night mode, Frontlight and Go Home rows.

## Changed

- **Text anti-aliasing is always off.** A page turn on the X4 Pro no longer waits for the grey pass, which was about 467 ms of a 1215 ms turn. The Text Anti-Aliasing and Text Contrast settings are removed. Pages with images keep their grey pass.
- **Fill Pages is always on.**
- **The next chapter lays out while you read,** so turning into it does not wait.
- **Defaults match CrossPoint:** Time to Sleep 10 minutes, the light comes back on wake, Power needs a 400 ms hold. Saved settings are kept.
- **Battery:** the X4 Pro drops its clock 1 s after input, the file server and Calibre screens no longer spin while idle, and Auto Sync joins Wi-Fi once per lock.
- **Typography and controls** ranges match CrossPoint.

## Removed

- The reading journal and Stats screen, What's New, Performance Timings and the `/perf` log, and the `.pxc` custom wallpaper format.
- Time Zone rows on the X4, which has no clock chip.

## Fixed

- **Auto Sync** pushes every book read since the last sleep.
- **Sleep screen** paints in one pass on grayscale panels.
- **Idle timer** ignores time spent blocked.
- **Dictionary:** four upstream fixes.
- **JPEG:** the cache band follows the decoder's scaled block size.
- **OPDS:** the header editor shows the saved value.
- **Touch:** a long press no longer also fires a tap.
