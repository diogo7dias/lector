# Lector 0.63.0

## Changed

- **One swipe scrolls a list a whole screen on the X4 Pro.** In Settings, Library, File Browser, Wi-Fi and other lists, a quick flick or a slow drag moves one full screen, and the list stops at its first and last screens instead of jumping back to the top. Side keys still move one row at a time.

## Faster

- **Locking with a Custom wallpaper is faster on a big sleep folder.** The reader now remembers where each wallpaper sits in the folder and opens it straight away, instead of searching the folder by name every time. After the update, the first wake after the first lock shows "Indexing wallpapers" once while the list is rebuilt. Later wakes do not.
- **Grey wallpapers lock about a second faster on the X4 Pro.** The picture is read and prepared once instead of three times.

## Fixed

- **A chapter with a broken image no longer crashes the reader** while the chapter lays out in the background.
- **A failed read from the memory card stops the chapter or cover safely** instead of crashing the reader.
- **The reader menu no longer freezes** when the device is too short of memory to open Reading Themes or Steal Look. The menu stays usable.
- **A damaged 2-bit XTC book is refused** instead of reading past the end of its page. The reader goes back, as with any other damaged book.
- **A reset while a book's images are being saved** no longer leaves a cut image that shows as broken from then on.
- **Hint buttons at the bottom of the screen always have their left border** on the X4 Pro, also next to an empty slot.
- **Long Quick Panel labels stay inside their button** and end in "…" instead of running past the edge.
- **German Resume button reads "Weiter"** and fits on one line.
