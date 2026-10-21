# Lector 0.44.0

## Added

- **Reading journal and Stats**: each reading sitting is logged to `/stats/journal.txt`. A new **Stats** row on Home shows the last 7 days, this year, all time, books finished and this month as a calendar. The web page has a `/stats` page too.
- **Show Reading Stats** (Settings > Display > Wallpaper): a black band with the last 7 days of reading along the foot of the sleep screen. Off by default.
- **Chapter Title** and **Book Title** status bar items. Placed on the same edge, they stack: book first, chapter below. An old Title Source = Book setting moves onto Book Title by itself.
- Hold a word on the page to look it up.
- Signed firmware: Check for updates installs only images signed by lector. An unsigned file from the SD card or Install Other Firmware asks first; a broken signature is refused everywhere.
- Your own OPDS and KOSync servers are now verified over TLS. A self-signed server asks once to trust its certificate, and asks again if that certificate changes.

## Changed

- Less ghosting on X4, X4 Pro and X3: page turns drive every pixel, and Home, lists, settings and popups repaint clean when you open or close them (about half a second each).
- X4 Pro `.pxc` wallpapers keep their brightness; mid greys are no longer darker.
- Customise Status Bar inside a book now sets the bar for every book; hiding the bar stays per book.
- Smart sync asks before uploading when this device is ahead.
- Lines break after hyphens and dashes; the chapter list opens on the sub-chapter you are in.

## Fixed

- OPDS shows "Server not reachable" when the server never answers, instead of "Failed to fetch feed".
- Leaving Settings > WiFi turns WiFi off again, freeing its memory.
- Corrupt XTC, OPF and quote files can no longer crash the reader or wipe saved quotes.
- Diagnostics keep crash records and log why File Transfer closed or an upload failed.
