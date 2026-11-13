# Lector 0.56.0

Every book re-indexes once when you first open it, because the layout cache changed. Reading positions are kept.

## New

- **X4 Pro: faster page turns.** On X4 Pro units with the UC8279 screen, a page turn takes about half a second instead of 0.7. Lector now drives the screen with its own short waveform instead of the panel's built-in one; the regular clean refresh still clears any build-up. X4 Pro units with the SSD1677 screen are unchanged.
- **Quick page turns are kept.** Presses made while the screen is still refreshing used to be lost. Up to four turns either way are now counted and shown together, and an opposite press cancels one.
- **OPDS: hold Back to go Home.** In a catalog, a short Back still goes up one level; hold it for a second to return Home.

## Fixed

- **Heading groups** keep the book's margins and alignment, and the space after a chapter title no longer lands below the first paragraph.
- **Buttons:** a side key bound to the other page direction now pages that way. Force refresh works on the Confirm long-press and the menu hold. Sleep bound to a reader hold now lets auto-sleep use the full timeout.
- **Reading journal:** paging back from the end of a book un-finishes it.
- **Book images** are written whole before use, so a reset mid-write no longer leaves a cut image.
- **TrueType fonts** space letters more evenly.
- **Dictionary** lookups find Hebrew and Arabic words written with vowel marks.
- **X4 Pro:** a restart after Auto Sync can no longer crash on a too-small system stack.
- Sortes skips Lector's own folders, and the latin-ext SD font preset includes combining marks.
