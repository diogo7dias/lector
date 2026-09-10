## Fixed
- **X4 Pro bottom buttons work on list screens.** Back, Select, Up and Down on the painted hint band were dead on screens that only look for a button release, including Customise Status Bar opened from a book. A hint-band tap now arms the next-frame release even when the first query is `wasReleased`. The shared option popup no longer swallows those taps. Hits are converted into the Portrait space the band is painted in, so a rotated book no longer misses the slots.

## Notes
- An X4 Pro already on 0.30.0 or 0.30.1 can OTA this image (`firmware-x4pro.bin`). An X4 Pro still on 0.29.5 cannot; it still needs the USB or SD hop first.
