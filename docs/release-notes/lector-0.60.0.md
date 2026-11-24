# Lector 0.60.0

## Changed

- **Sleep and wake refresh like CrossPoint.** On the X4 and X4 Pro the sleep screen clears with one white pass instead of four full refreshes, so a grayscale wallpaper takes 4 panel passes instead of 7. Pages with images get one base refresh. The display driver no longer overrides the refresh mode a screen asks for.
- **Fast Unlock is off by default,** so a wake from a wallpaper runs a clean pass and the first page shows no ghost of it. A saved setting is kept: turn it off in Settings > Display if your device still has it on.
- **A chapter finishes its layout while you rest on a page.** After 400 ms without input the background build runs to the chapter's end, so a chapter reached by a contents or bookmark jump, the first chapter of a book, or a very long chapter no longer lays out again on the next visit. Page turns still go first.

## Fixed

- **Reopening an unfinished chapter** shows the page straight away, with no Indexing popup and no rebuild first.
- **Custom sleep screen** no longer falls back to the book cover. It tries the favorite-renamed file, retries the pick, and scans the folder before it falls back to the Lector face, and only when no wallpaper can be drawn.
