# Lector 0.48.0

Every book re-indexes once when you first open it, because the layout cache changed. Reading positions are kept.

## New

- **Publisher styling**: drop caps, borders and shading around blocks, small caps and per-word type sizes follow the book's own style. Type sizes need a font on the SD card with more than one size. Tables plan their column widths, and orphan lines and trailing headings carry to the next page.
- **Pinch to resize**: on a touch screen, spread or pinch two fingers to change the text size by one step, per book. Needs a font with more than one size.
- **Character Spacing** in Text Settings, -5 to +5, next to Word Spacing.
- **Go to Page**: in EPUB 3 books with a page list, type a printed page number from the Navigate menu. The status bar page item adds the printed page.
- **Number pad on Go to %** on touch screens.
- **Small images inside the text line**: an image up to two lines tall wraps like a word. Larger images stay centred blocks.
- **Rename** in the file browser: hold Confirm on a file. Its place, bookmarks and highlights follow it.
- **Quick Lock**, a new button action in Settings > Controls > Buttons: the screen stays up and touch and buttons stop until the same gesture fires again. Any sleep ends it.
- **Light Schedule** in Settings > Display, on boards with a frontlight and a clock: a wake inside the set hours lights the screen.
- **Screen Calibration** in Settings > Display: move each edge of the page in from the bezel, 0 to 32 px. It is stored on the device, not the card.
- **What's New** appears once after an update, and stays in Settings > System.

## Changed

- BMP wallpapers larger than the panel are shrunk before dithering, so thin lines survive and seams are gone.
- A dictionary lookup shows every entry for the word, finds words wrapped in `「」` or `《》`, and reads dictionaries that tag each entry's fields.
- Option pop-ups too long for the panel now split into pages, so every button action can be seen.
- More free memory: the settings list, spine sizes and SD font tables no longer stay in RAM, and File Transfer uses less while it runs. File Transfer allows two WebSocket connections and closes an upload client that stays silent for two minutes. The upload can resume.

## Fixed

- Selecting a word, Grab Quote and quote underlines follow words set in a different type size.
- Words no longer glue together across a block start, and justified lines no longer run past the margin. Inherited bold and italic are kept, and lines in SD fonts end at the same x position.
- A power loss while saving can no longer leave two files sharing the same data on the card.
- Back leaves File Transfer during a slow upload, and removes the partial file.
- USB Drive ends when the cable is pulled from a battery-powered X4 Pro.
