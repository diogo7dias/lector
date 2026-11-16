# Lector 0.58.0

Books re-index once: the layout cache format changed (book v14, section v88), and inline images decode again.

## Changed

- **X4 Pro page turns back on the stock waveform.** UC8279 units use the panel's own fast waveform again, instead of the 12-frame one from 0.56.0.
- **Time to Sleep defaults to 5 minutes** on a fresh device. A saved setting is kept.
- **Performance log:** with Show Timings on, the log records battery, millivolts and time at each wake and sleep.

## Fixed

- **Sharper JPEG images:** high-quality inline JPEGs now decode as libjpeg does, without smeared edges on text and line art.
- **Images after filing:** a book with images no longer shows empty boxes after you leave it and open it again.
- **Image paths:** images and chapters linked as `./name` now show up.
- **Battery:** the update, font download and clear cache screens, the Nearby position screens, and Settings left from the Wi-Fi list no longer keep the device awake or Wi-Fi on.
- **Saved files:** the SD card is flushed before sleep and before USB Drive, so recent changes are kept.
- **Network:** the OPDS password goes only to its own server. WebDAV rename, copy and upload no longer lose a book, and downloads land whole or not at all.
- **X4 Pro:** the keys wake the device from the idle poll, and the light sliders nudge to 0% and 100%.
- **Display:** accent marks the font lacks no longer stamp a box over the letter, and bottom-up BMPs draw fully.
- **XTC:** the bit depth comes from the pages.
- **Crash reports** name the functions at each address.
- **Memory:** fixes for bugs found by new fuzz tests, including a 16 KB leak when SD fonts load.
