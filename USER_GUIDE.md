<!-- lector-version: 0.65.0 -->

# Lector User Guide

Lector is e-reader firmware for the Xteink X3 and X4. This guide covers the buttons, the
screens, the in-book menu, and every setting the firmware ships with.

A few names on the device and on the SD card still read "CrossPoint", the project Lector
forked from: the `/.crosspoint` folder on the card, the `crosspoint.local` web address, and
the CrossPoint Reader plugin for Calibre. Those are written exactly as they appear.

Contents:

- [1. Buttons](#1-buttons)
- [2. Power, sleep and wake](#2-power-sleep-and-wake)
- [3. Home screen](#3-home-screen)
- [4. Browsing files](#4-browsing-files)
- [5. Reading](#5-reading)
- [6. The in-book menu](#6-the-in-book-menu)
- [7. Bookmarks and quotes](#7-bookmarks-and-quotes)
- [8. Sleep screen and wallpapers](#8-sleep-screen-and-wallpapers)
- [9. Getting books onto the device](#9-getting-books-onto-the-device)
- [10. Syncing reading position](#10-syncing-reading-position)
- [11. Fonts and dictionaries](#11-fonts-and-dictionaries)
- [12. Settings reference](#12-settings-reference)
- [13. Updating the firmware](#13-updating-the-firmware)
- [14. Troubleshooting](#14-troubleshooting)

---

## 1. Buttons

Lector uses the buttons the device already has, in the manufacturer's layout by default.

| Location    | Buttons                                  |
| ----------- | ---------------------------------------- |
| Bottom edge | Back, Confirm, Left, Right               |
| Right side  | Power, Volume Up, Volume Down, Reset     |

Throughout this guide the side buttons are called **Volume Up** and **Volume Down**, and the
bottom-edge buttons **Back**, **Confirm**, **Left** and **Right**.

**Back** returns to the screen you came from. From a screen a menu or pop-up opened, it lands on
that menu again, on the row you chose; choosing something in that screen (a chapter, a
bookmark, a Yes) goes on as before. File Transfer opened from Settings comes back to Settings,
and a file browser, Nearby, OPDS or USB Drive screen comes back to the screen that opened it,
on the row you chose. Settings opened from the quick panel comes back to the quick panel. Inside a book,
**Back** from the reader menu returns to the page. The Home key and the swipe up from the
bottom edge go straight Home.

A screen that has had Wi-Fi on restarts the reader when you leave it, to give the memory back.
The restart lands where **Back** would have: the Settings card or Home row, the OPDS server or
the File Transfer mode you came from. The Home key and the Home gesture still land on Home.

The four bottom-edge buttons can be reassigned in **Settings > Controls > Remap Front Buttons**,
and the side buttons can be swapped or disabled with **Side Button Layout (reader)**.

### Touch page turning

**Settings > Controls > Touch Reader Controls** switches touch page turning on or off. With it
on, **Next Page** and **Previous Page** each take their own gesture: Tap & Swipe, Tap Only,
Swipe Only (the default), Inverted Tap or Disabled. A swipe to the left goes on, a swipe to the
right goes back. Taps turn from the outer thirds of the page, or from the whole page when only
one direction accepts taps; the centre of the page opens the reader menu. Inverted Tap on either
direction swaps the two sides. In a right-to-left EPUB (language Hebrew, Arabic or Persian) the swipes and
the tap sides run the other way. Older settings files carry over: the old Tap, Swipe and
Inverted Tap become the matching gesture for both directions.

**Show Reader Menu** (Settings > Controls) turns the centre tap **Tap** or **Off**. Inside a
book, a swipe up from the bottom edge always opens the reader menu.

### Tapping, on a device with a touch screen

A tap runs what it lands on: a row opens, a button acts. Back undoes a wrong open.

A few actions cannot be undone, so they take a second tap: Clear Reading Cache, Clean Up
Storage, a firmware update, forgetting a Wi-Fi network, Delete Cache in the reader menu, and
the Confirm button of every "Delete?" question. The first tap draws a dashed outline round
that button and does nothing else; a second tap on the same button runs it. A tap anywhere
else, a scroll or a side key drops the outline.

A question asks once. On the X4 Pro it shows its two answers as buttons at the foot of the
screen and no button band; on the X3 and X4 the band names what Back and Confirm answer.

On the X4 Pro a list arrives with no black row: a tap opens what it lands on, so a selection
means nothing to a finger. Press a side key and the black row appears and walks with the key.
The band along the bottom has no keys behind it, so it stops pretending to be them: Back on
the left; the second slot names what it does to the black row (Open, Change, Turn on, Turn
off) and stays blank until a side key showed one; Page Up and Page Down on the right, shown
only while the list runs past one screen.

On every device a list that scrolls shows which screen of it you are on beside the battery,
as 2/5, and the Settings band says what Confirm does to the row it is on: Open, Change, Turn
on or Turn off, instead of Toggle.

A swipe up from the bottom edge opens the reader menu inside a book, and goes Home from
every other screen; the capacitive Home key below the screen goes Home everywhere. A swipe
up that starts higher on a list scrolls it a whole screen at a time: the first row below the
screen becomes the top row, and a swipe down goes back a screen. A quick flick and a slow
drag both move one screen per swipe, and the list stops at its first and last screens. A
list that fits on one screen does not move. The side keys still move one row per press. On
Home a flick up or down over the list pages through the books you are reading.

Page turns by tap while reading are unchanged. Holding a word on the page for half a second looks it up in
the dictionary; keep the finger down and drag to select a phrase instead (see **Quotes**).
Spreading two fingers apart on the page makes the text one size
larger, pinching them together one size smaller; one step per gesture, for this book only,
like the size row in the quick panel. With ChareInk it steps through 12, 14 and 16 pt.

On the keyboard, tap inside the text to put the cursor there; the next key types at that
spot. In a password field, tap `[abc]` at the right of the field to show the password and
`[***]` to hide it again. On the X3 and X4 the same two are **Up** held (cursor) and then
**Right** held (show or hide).

**Touch Sleep** (Settings > System, X4 Pro) is Off by default. Set to 15 s, 30 s or 1 min,
it saves a little battery by putting the touch screen to sleep after that long without
input. While it sleeps, touches and the touch Home key below the screen do nothing; press any
side button, or lock and unlock, to wake it.

**Wake on USB Plug-In** (Settings > System, X4 Pro) is Off by default. When on, plugging in a
USB cable wakes the sleeping device, so it shows up on the computer without pressing Power.
The wake comes from the charger starting to charge, so a cable that does not start a charge
(some chargers with a full battery) does not wake it, and neither does a cable that was
already charging when the device went to sleep. Press Power then.

### Taking a screenshot

Hold **Power** and **Volume Down** together. The image is written to `/screenshots/` on the
SD card as a BMP file.

Screenshots can also be taken from inside a book: open the in-book menu, go to the **Device**
tab, and pick **Take screenshot**. A screenshot taken this way is filed under a folder named
after the book, and its filename records the chapter, page and percentage.

---

## 2. Power, sleep and wake

### Power on and off

Press and hold **Power** for about half a second.

With **Settings > Controls > Short Power Button Click** set to **Sleep**, the hold that puts
the device to sleep is much shorter (about a tenth of a second); every other value keeps the
longer hold, so a stray press cannot switch the device off.

Waking is a separate setting: **Settings > Controls > Wake Hold**. **Normal** needs the usual
half-second hold, so a press in a bag cannot wake the reader; **Fast** wakes on any press.
Readers that already had Sleep selected keep the fast wake they had.

Binding an action to **Double-click power** turns that shortcut off again: the first of the
two presses would otherwise sleep the device before the second one arrived. Sleep then needs
the normal half-second hold, and the double-click action works as bound.

To reboot, press and release **Reset**, then press and hold **Power** for a few seconds.

Boot shows the name "Lector" between the two banners. Waking never redraws the sleep
screen: the unlock goes straight to the screen you asked for.

### Battery

The battery percentage sits in the status bar and in the home header. When the charge drops
to 10 percent Lector shows a **Battery low** notice over whatever you are reading; press any
button to dismiss it and carry on. It appears once per discharge, not once per wake, and it
arms itself again after the battery climbs back above 15 percent. It stays quiet while the
device is plugged in, and it never interrupts a firmware update, a file transfer or the
sleep screen.

### Sleep

The device sleeps after the inactivity set in **Settings > System > Time to Sleep**, anywhere
from 1 to 30 minutes, or Never. The default is 10 minutes. What it shows while asleep is the sleep screen, covered in
[section 8](#8-sleep-screen-and-wallpapers).

A wake from sleep goes straight back into the book, without stopping at any other screen.

**Fast Unlock** (Display settings, off by default) shortens the wait for the recovery button
chord at wake and paints the page straight over the sleep screen in one pass instead of
clearing the screen first; the first page turn after a wake then runs the clean pass. Leave
it off if a wake ever lands in the recovery firmware picker by itself, or if a faint
wallpaper stays visible in the first page after a wake.

### First launch

The first boot lands on the [Home screen](#3-home-screen). After that, an unlock always
opens a book. **Open Book on Boot** chooses which: **Last Book** (the default) opens the
last-read book every time, and **Random Book** opens one of the books in progress at
random. Holding Back during boot skips both and lands on Home.

On a fresh card, Lector creates the folders it uses: `/read`, `/recents`, `/sleep` and
`/sleep pause` (that last name contains a space).

---

## 3. Home screen

The Home screen is the entry point. It lists the books you have been reading, and below them:

- **Continue Reading** — reopen the most recent book. Present in themes that put it in the
  menu, and only once something has been read.
- **Browse Files** — the file browser.
- **Library** — every book on the card by title, see [Library](#library).
- **OPDS Browser** — shown once at least one catalog is configured, see
  [section 9](#9-getting-books-onto-the-device).
- **Nearby Sync** — receive a reading position or send and receive files with another Lector nearby, see
  [section 10](#10-syncing-reading-position). Wi-Fi, Hotspot, Calibre and USB Drive live under **Settings > File Transfer**.
- **Quick Panel** — the X3 and X4 only: opens the quick panel (see Settings reference), the same
  panel a double click of **Power** opens.
- **Settings** — see [section 12](#12-settings-reference).

**Settings > Controls > Home Back Button** decides what **Back** does here: nothing, resume
the current book, or open **Sortes**.
Resume remains the default. Sortes opens a randomly chosen finished EPUB at a random page:
a book counts as finished when its saved library badge is 100% (Read), wherever it is filed.
The footer says **No read books** and does nothing when none qualify.

Back or sleep ends the visit. Sortes leaves the saved reading position, Read status, filing
and recents untouched. Quotes and bookmarks can still be saved deliberately, and the reader
menu works. Book look edits last for this visit;
sync, sending the book, deleting its cache/book and removing it from recents require opening
it normally. Selection covers the whole library; an unreadable or over-deep directory tree
shows **Unavailable** instead of choosing from an incomplete scan.

**Settings > Display > Author On Home** switches the author line between initials and the
full name.

### Library

**Library** lists every book on the card in one list, sorted by title by default, with the author
under each title and each book's reading badge (`[NN%]` or **Read**) at the right. Pick one to open
it. The top right shows which book you are on and how many there are. Titles and authors come from
the books themselves (an EPUB's own title and author); a book without them is listed by its file
name. Books in any folder are listed, wherever they are filed (`/recents` and `/read` included), but
not the folders Lector keeps for itself (`/sleep`, `/fonts` and the like) or hidden files.

Open the **Quick Panel** while in Library to change **Sort** (**Title**, **Author**, **Recently
Added** or **Last Read**) and **Filter** (**All**, **Reading**, **Unread** or **Read**). Hold
**Confirm** on a book (on the X4 Pro, press and hold the row) for **Book Info** (EPUB only),
**Mark Read** or **Mark Unread**, and **Delete**.

The list is read from an index Lector keeps on the card (`/.crosspoint/lector-library.idx`).
The first visit after the device starts, and the first one after any book was added, moved,
renamed or deleted, rebuilds it: a strip says **Reading your books...** while it does. A
card with a few hundred new EPUBs takes a while the first time, because each is opened once
for its title; later visits only read the books that changed. It lists at most 2048 books on
the X3 and X4 and 4096 on the X4 Pro. If the card is empty the list says **No books found on
the SD card**.

**Search all books**, the first row, searches every book on the card, not just one folder.
Type a word and press **OK**: the list becomes the books whose title or author matches, best
first (a title that starts with it, then a word in it, then its letters in order, so `gtg`
finds *The Great Gatsby*). The search row shows the query and how many books matched; the
list keeps the best 64. **Clear search**, or **Back**, returns to every book.

### End of book

Turning past the last page shows **End of book** with the title and author, the day you
finished (when the clock is set), and the last quote you saved from it. Any books from the
same folder follow under **Continue with**.

---

## 4. Browsing files

The file browser shows the folder name at the top, with the order the books are in beside it
(**Alphabetical**, **Random**, **Recently Added** or **Last Read**, changed from the Sort row
of the light panel), files with their extensions, folders in brackets, for example
`[folder-name]`, and the folder's path at the foot of the screen. On the X4 Pro, tap the path
to go up one folder.

- **Move the cursor** — **Left** (or **Volume Up**) and **Right** (or **Volume Down**). Hold
  either one to move a whole page at a time.
- **Open** — **Confirm** opens the folder, book or image under the cursor.
- **File actions** — hold **Confirm** on a file (on the X4 Pro, press and hold the row) to
  open a small menu: **Book Info** (EPUB
  only), **Mark Read** or **Mark Unread** (EPUB and TXT), **Send to Nearby Reader** (files another
  reader can accept), **Rename**, **Move to folder** and **Delete**. **Book Info** shows the title,
  the cover (when the book has
  one), the author and the publisher's description, paged with the page buttons; **Back**
  returns to the list.
  A folder goes straight to the delete confirmation. Nothing is deleted without confirming.
- **Rename** — edit the name on the keyboard; the extension stays. Reading position,
  bookmarks, highlights and saved quotes follow the book, as they do when it is moved.
- **Search** — search inside the current folder, with the results ranked by how well they
  match.

Files can also be renamed and moved from the web interface, see
[section 9](#9-getting-books-onto-the-device).

Each row shows its file type on the right, and a book you have opened before also shows how
far in you are on the left, in front of the title: a percentage while you are reading it, or
**Read** once you reach its last page, in the same style the home screen uses. The
badge stays even after the book leaves the Recents list or is filed into `/read`. Books you
have never opened show no badge, and comics (XTC) never carry one.

Upgrading from an older release, the books still in your Recents list get their badge on the
first boot; any other book read long ago gets one the next time you close it.

**Settings > System > Show Hidden Files** decides whether entries beginning with `.` appear. The
`System Volume Information` and `XTCache` folders never appear, and their contents are never
picked as books. The files holding your Wi-Fi, OPDS and sync passwords and your trusted server
certificates are never shown or served over the network, even with this setting on.
**File Browser Order** lists a folder alphabetically, in random order, by the date each file
was added, or by when you last read it. Both date orders put the newest first and leave the
folders at the top; books you have never opened sit at the bottom of Last Read, in name
order. Last Read reads one small file per book, so a folder of hundreds takes a moment
longer to open.

Recently Added uses the date on the file itself, which is written by whatever put it there:
books copied from a computer carry a real date, while books the device downloaded itself
(Wi-Fi transfer, Calibre, OPDS) all share one, so they keep name order among themselves.

### What Lector can open

| Type            | Opens as                                              |
| --------------- | ----------------------------------------------------- |
| `.epub`         | The full reader: chapters, footnotes, bookmarks, quotes |
| `.txt`          | Plain-text reader: per-book Text Settings, Reading Themes, justification |
| `.xtc`          | XTC reader, the Xteink book format                    |
| `.bmp`, `.png`, `.jpg` | Image viewer                                   |
| `.pxc`          | Image viewer for the packed wallpaper format          |
| `.bin`          | Offered as a firmware file, see [section 13](#13-updating-the-firmware) |

Text files are read as UTF-8. A file that is not valid UTF-8 is read as Windows-1252, the
usual encoding of older Western text files (Latin-1 accents, curly quotes, the euro sign).
UTF-16 files are not supported and show a message instead.

From the image viewers you can set the picture as your sleep wallpaper. A `.jpg` is converted
to a panel-sized `sleep.bmp` (Fit or Crop, as **Sleep Screen Cover Mode** says); a `.bmp` is copied as it is.

On the X4 Pro, tap the left third of the screen for the previous picture in the folder and
the right third for the next one. A tap in the centre opens a menu with what the buttons do
there: **Set Cover** outside the sleep folders, or the three triage actions inside them.
Delete still asks before it deletes.

Opening a wallpaper from `/sleep` gives you the triage buttons: **Favorite**, **Pause** and
**Delete**. Favoriting closes the viewer straight away and takes you back to the folder, so
you can work through a folder at button speed. The rename itself happens later, when you
open a book, reach the home screen, or lock the device, because renaming a file in a folder
of thousands is slow enough to be worth keeping off the press. The list shows the new name
immediately either way.

### Filing books automatically

Three System settings move books for you as you read:

- **Move Finished Books to Read Folder** — a finished book moves to `/read`.
- **Move Opened Books to Recents Folder** — an opened book moves to `/recents`.
- **Clear Read Books from Recent List** — a finished book drops off the recents list.

---

## 5. Reading

### Turning pages

| Action        | Buttons                          |
| ------------- | -------------------------------- |
| Previous page | **Left** or **Volume Up**        |
| Next page     | **Right** or **Volume Down**     |

**Settings > Controls > Side Button Layout (reader)** swaps the two side buttons, makes both turn
the same way (Next/Next or Prev/Prev, for one-handed reading) or disables them while reading. **Orient front buttons** turns the bottom-edge buttons along with the
page when you read in landscape.

With **Short Power Button Click** set to **Page Turn**, a short press of **Power** also turns
the page.

In EPUB books, page turns pressed while the screen is still refreshing are counted, up to four
either way, and applied together once it is free: the reader goes straight to that page and
draws only it. A run stops one page past the end of the chapter (or past the pages laid out so
far); the extra presses are dropped.

EPUB, TXT and XTC batch position saves every ten page turns, or on the next page redraw
after five minutes. Leaving the book or putting the reader to sleep saves any pending
position; explicit progress sync saves immediately.

### Chapters

Chapter jumps live in the in-book menu, under **Navigate > Select Chapter**. Page turning
fires on the press, so the page never waits to find out whether a hold was coming. In an
`.xtc` book, holding a page button for 0.7 seconds also jumps ten pages in that direction
(the one page the press turned counts toward them), once per hold. Elsewhere holding a page
button does nothing, unless you bind that hold in **Buttons**, which then takes precedence.

### Footnotes

In an EPUB with footnotes, opening a footnote shows its note text in a pop-up card over the
live page without leaving the chapter. On X4 Pro, tap the footnote link directly on the page to
open the card; on any device, open it from **Footnotes** in the in-book menu or via a bound
shortcut. Long notes page inside the card and include a **Jump to Note** button to open the full
endnote page. Press **Back** or tap outside the card to close it. Linked EPUB 3 `<aside>`
footnote blocks stay out of the main reading flow and appear in the pop-up when opened.

When you jump to an endnote page, falling asleep or closing the book reopens on the page you
were viewing, and **Back** still returns you to where you followed the link.

**Short Power Button Click > Footnotes** opens the footnotes for the page with a short press;
when the page holds a single footnote, it opens that one directly.
**Quick-return from footnotes** makes a short power press act as **Back** while a footnote is
on screen.

### Leaving the book

- **Back** closes the book and returns to where you opened it: the file browser folder, the
  Library, or Home for a book opened anywhere else. This survives sleep.
- **Hold Back** returns to the file browser, opened in the book's own folder. **Settings > Controls > Short Back to File
  Browser** swaps the two, so a short press goes to the browser instead.
- **Confirm** opens the [in-book menu](#6-the-in-book-menu).
- **Hold Confirm** runs whatever **Settings > Controls > Long press while reading** is bound
  to. A short press always opens the menu.

### Holds and double clicks

Three Controls settings bind an action to a gesture. They share one list of actions, so any
of them can be pointed at any action.

- **Long press while reading** — holding **Confirm** in the book.
- **Hold in in-book menu** — holding **Confirm** with the menu open.
- **Double-click power** — two quick presses of **Power**.

On the X4 Pro a fresh device starts with a double press of **Home** and a double click of
**Power** bound to **Toggle Frontlight**, and holding **Home** in a book bound to **Open Reader
Menu**. On the X3 and X4 a fresh device starts with a double click of **Power** bound to
**Quick Panel**. All of these are ordinary actions in the list, so any button can take them; a
binding you have already saved is kept as it is. Every other gesture starts unbound.

**Pop-up Items** decides which rows the quick pop-up those bindings can open actually
contains.

A list too long for the screen, such as the actions in **Buttons**, splits into pages and
shows `1/2` beside its title. The side keys walk from one page to the next; on a touch screen,
flick up or down.

**Quick Lock** is on offer only in **Settings > Controls > Buttons**: bind it to a button
gesture, in the book, outside it or both. Firing it leaves the screen as it is with a
**Locked** badge in the corner, and touch and every button stop answering until you fire the
same gesture again. Holding **Power** still sleeps while that hold is set to Sleep, **Time to
Sleep** still runs (pressing things does not hold it back), and any sleep ends the lock. It
is refused while a transfer is running or the quick panel is open.

Text Settings is no longer one of the actions on offer, and is no longer a pop-up row.
It is reached from Settings, or from the in-book menu. A binding that pointed at it
reads as Disabled after the update; pick another action for that gesture.

### Reading in landscape

**Settings > Reader > Reading Orientation** offers Portrait, Landscape CW, Portrait 180° and
Landscape CCW. The in-book menu can rotate the screen without leaving the book.

---

## 6. The in-book menu

Press **Confirm** while reading. The menu is one list of five tabs: **Look**, **Navigate**,
**Book**, **Sleep Screen** and **Device**. Each tab is a heading with a plus at its right edge
and the number of rows it holds. Moving onto a heading opens it (the plus turns to a minus)
and closes the one that was open. **Back** closes the open tab; a second **Back** leaves the
menu. From the tab the menu opened on, one **Back** leaves the menu. Rows that do not apply to
the open book are not shown at all.

The menu opens with **Sleep Screen** expanded when the lock screen has a wallpaper to act on, or
with **Navigate** expanded otherwise.

### Navigate

- **Select Chapter** — the table of contents, with the current chapter marked (`>`) and each
  chapter's book percentage right-aligned. Chapters the book lists but cannot actually
  open are left out, so every row here goes somewhere.
- **Go to %** — jump by percentage. The front buttons move it by 1, the side buttons by 10.
  On a touch screen a number pad sits under the slider: type the number (4, 7 for 47%),
  the delete key takes back the last digit, and **OK** jumps.
- **Go to Page** — in an EPUB 3 book that carries printed page numbers (a page list), jump to a
  printed page by typing it as the book prints it (`147`, or `xii` in the front matter) and **OK**.
  In every other EPUB, jump to a page within the current chapter (`1` to the chapter's total, shown
  as `N/M` in the prompt). Entering a page that does not exist shows **Page not found**; Return
  takes you back.
- **Return** — go back to where you were before selecting a chapter, jumping by percentage,
  or opening a bookmark. Keeps the last eight jump origins and survives sleep and reopening
  the book; ordinary page turns do not add entries. Shown only while there is somewhere to
  return to, and also bindable to a button in **Settings > Controls > Buttons**.
  Changing text size still returns to the same passage; the Back button and footnote returns
  keep their usual behavior.
- **Forward** — after a Return, go back to the page you returned from. Shown only after a
  Return. A new jump (chapter, percentage, bookmark) clears it.
- **Go to Paragraph** — jump by paragraph number, shown only while paragraph numbering is on.
- **Bookmarks** — the list, shown once the book has one.
- **Toggle Bookmark** — drop or lift a bookmark on this page.
- **Footnotes** — shown only in books that have them.

### This Book

- **Look Up** — pick a word on the page and read its definition. Needs a dictionary, see
  [section 11](#11-fonts-and-dictionaries).
- **Grab Quote** — save a passage from this page.
- **View Quotes** — read back what you saved, shown once the book has quotes.
- **Remove This Book** — unfile the book from recents; the file stays on the card.
- **Delete Book** — erase the file, after a confirmation.

### Look

- **Reader Settings** — text settings for this book alone.
- **Reset Reader Settings** — drop the per-book override, shown only when one exists.
- **Steal Look** — copy another book's look onto this one, once.
- **Reading Themes** — apply a saved look. A theme holds the text, margin and in-book settings, the paragraph numbers and the status bar layout (which items show and where). A theme's bar becomes your default bar, like Customise Status Bar. Up to 8 themes are kept.
- **Paragraph Numbers**, **Number Size**, **Number Position**, **Paperback Look**, **Paperback Status**.
- **Status Bar** — show or hide it. With the bar hidden, a **Progress Bar** row appears in its
  place.
- **Customise Status Bar** — everything the bar can show and where. Changes here become your
  default bar, so every book opens with it.
- **Orientation** — rotate the screen.
- **Night mode** — flip the page to white on black and back. The same switch as the **Night
  mode** setting; the menu itself stays light.
- **Frontlight** — turn the light on or off, without leaving the book. Shown only on a reader with a
  frontlight. For brightness and warmth, swipe down from the top edge.

### Sleep Screen

This tab appears only when the last sleep screen was a wallpaper that is still on the card.

- **Favorite** or **Unfavorite** this wallpaper.
- **Pause Wallpaper** — move it out of the rotation, into `/sleep pause`.
- **Hold This Wallpaper** or **Resume Wallpaper Rotation** — keep showing this one, or start
  rotating again.
- **Delete Wallpaper** — erase the file.

### Device

- **Take screenshot**.
- **Show page as QR**.
- **Sync Progress** — KOReader sync, see [section 10](#10-syncing-reading-position).
- **Nearby Sync** — take a position from another Lector nearby.
- **Send to Nearby Reader** — send this file to another Lector nearby.
- **Delete Book Cache** — clear this book's layout cache; it is rebuilt on the next open.
- **Go Home** — leave the book for Home. Your place is kept.

Press **Back** to close the menu. **Back** from a screen a row opened (Text Settings, Customise
Status Bar, the chapter list, Bookmarks, Sync Progress and the rest) comes back to this menu, on
that row; choosing something in that screen (a chapter, a bookmark) goes to the page.

### Toolbar style

**Settings > Reader > Reader Menu Style** swaps the list for a toolbar: a sheet over the bottom of
the page, with the page above it left as it is. The same press, tap or bound action opens it. It applies to
EPUB books.
It holds:

- A **Return** chip above the scrub row whenever there is a jump origin to return to; tap it to
  jump straight back to where you were reading.
- A **scrub row**: drag along the track, or tap it, to jump through the book by chapter (or by
  page in books with one or two spine sections); the arrow buttons at each end step one chapter or
  page. Dragging updates the chapter title and percentage live before you lift your finger. The
  line under it names the chapter and gives the page and the percent.
- Four tiles: **Contents** (the chapter list), **Bookmark** (toggles the bookmark on this page;
  filled when bookmarked), **Text** (this book's text settings) and **More** (the tabbed menu
  above).

Tap a tile to open it, or tap the page above the sheet to close it. On a reader with buttons,
**Left** and **Right** move between the tiles, **Confirm** opens the marked one, **Up** and
**Down** step a chapter (or page on short books), and **Back** closes. **Menu Hold** does nothing
while the toolbar is up. **Back** from the screen a tile opened brings the toolbar back.

---

## 7. Bookmarks and quotes

### Bookmarks

Bookmarked pages show a folded dog-ear in the top-right corner of the page. On the X4 Pro, tapping
the top-right corner toggles the bookmark on that page directly. You can also bind **Long press
while reading** to **Bookmark** and hold **Confirm**, tap the **Bookmark** tile in the toolbar, or
use **Toggle Bookmark** in the menu. Open the list from **Navigate > Bookmarks**, press
**Confirm** on an entry to jump there. To delete one, hold **Confirm** on it, then confirm.
**Settings > Reader > All Bookmarks** lists the bookmarks of every book; picking one opens
that book at it, and **Return** goes back to where you had stopped. Older bookmarks show once
their book is in Recents or has been opened.

Bookmarks are stored as JSON under `/.crosspoint/bookmarks`.

### Quotes

**Grab Quote** saves a passage from the current page. Quotes for a book live beside it on the
card as a plain-text sidecar named after the book, for example `Book_QUOTES.txt`, with the
chapter and anchor recorded next to each quote. **View Quotes** reads them back on the
device.

On a touch screen, select a passage with your finger:

1. Hold a word for half a second, then drag without lifting. The highlight follows the
   finger. Lift without dragging and the word is looked up instead.
2. To change the selection, touch near its first or last word and drag that end.
3. Tap **Save** to keep the passage as a quote, or **Look Up** to look the whole phrase up in
   the dictionary. **Cancel** returns to the page.

Up and Down still move the end of the selection, onto the next page if needed. Inside
**Grab Quote**, touching a word starts a selection the same way, no hold needed. Look Up
appears only when a dictionary is set.

Every saved quote is also added to `My Clippings.txt` at the root of the card, in the Kindle
layout that Readwise, Calibre and most clipping tools import: book title and author, page,
chapter, the time it was saved (when the device knows the time), then the passage. Deleting
a quote in **View Quotes** removes it from the book's quotes file only; `My Clippings.txt`
keeps everything ever saved, so trim it on a computer if you need to.

With BookOrbit as the KOReader sync server, quotes also travel to BookOrbit as highlights
(see **BookOrbit** under KOReader Sync).

---

## 8. Sleep screen and wallpapers

**Settings > Display > Sleep Screen** decides what the panel shows while the device sleeps:

| Mode           | Behaviour                                                                        |
| -------------- | -------------------------------------------------------------------------------- |
| Dark           | The lector crow and the name "Lector", white on black. Drawn by the firmware, no SD file. |
| Light          | The same screen, black on white. Drawn by the firmware, no SD file. |
| Custom         | A wallpaper from the SD card. When none is found, the open book's cover; with no book open, the white crow and "Lector" screen. |
| Cover          | The open book's cover. With no book open, the white crow and "Lector" screen.     |
| Cover + Custom | The open book's cover when you lock from a book, a wallpaper otherwise. With no cover, the wallpaper; with no wallpaper, the white crow and "Lector" screen. |
| Quick Resume   | The page you were on stays on the screen with a small moon in the bottom-left corner, and waking puts the same page back. Nothing is cleared or redrawn at lock, so it is the fastest face; a copy of the screen is kept on the card in `.crosspoint`. |
| Transparent    | A picture with see-through parts (an overlay) drawn over the page you were on. See Overlay files below. With no overlay on the card, the white crow and "Lector" screen. |
| Cover + Transparent | The same overlay drawn over the open book's cover. With no cover, over a blank page; with no overlay, the plain cover. |
| Quote          | A quote of the day drawn from `/My Clippings.txt`, typeset in the reading font with the book title and author. Changes once per day when the clock is set (random otherwise). Falls back to Cover, or the Lector face, when `/My Clippings.txt` is empty. |
| None           | An empty white screen. Drawn by the firmware, no SD file. |

Custom is the default. Inactivity uses your selected sleep screen.

### Wallpaper files

Wallpapers are `.bmp` or `.pxc` files. The `.pxc` format is the packed 2-bit format the
[Lector converter](https://diogo7dias.github.io/lector-web/) writes.

- **A folder of wallpapers (recommended):** put files in `/sleep`. One is chosen each time
  the device sleeps. A hidden `/.sleep` folder is also read.
- **A single wallpaper:** put `sleep.bmp` or `sleep.pxc` in the root of the card. A root file
  takes priority over the folders.

Size the image to the panel: **480x800** on the X4, **528x792** on the X3. A larger
`.bmp` still works: with **Sleep Screen Cover Mode** on Fit, it is shrunk to the panel by
averaging its pixels, so thin lines and fine detail survive (up to 2048x3072).

Wallpaper rows in the in-book menu's Sleep Screen tab favourite, pause, hold and delete the
wallpaper the device just showed. Paused wallpapers move to `/sleep pause` and stop appearing
in the rotation. **Settings > Display > Shuffle Wallpapers** reshuffles the order.

Three Display settings decide what is drawn over a wallpaper: **Show Wallpaper Name**, **Show
Wallpaper Position** and **Sleep Footer Text** (your own line of text). **Sleep Image Quality** trades rendering time against how the image looks: **Fast** or
**Pretty**.

### Overlay files

The two Transparent faces read overlays, not wallpapers: a 32-bit `.bmp` with an alpha channel
or a `.png` (with transparency; grayscale, indexed or colour). Put one at the root of the
card as `sleep-overlay.bmp` or `sleep-overlay.png`, which takes priority, or several in
`/sleep-overlay` (a hidden `/.sleep-overlay` is read too) and one is picked at random each
time the device sleeps. A `.bmp` with no see-through pixels is not an overlay and is skipped.
Overlays follow **Sleep Screen Cover Mode** (Fit or Crop) like a wallpaper. In the image
viewer, **Set Cover** saves a `.bmp` or `.png` as the root overlay while a Transparent face is
selected.

### Cover settings

For **Cover**, and for the cover Custom falls back to:

- **Sleep Screen Cover Mode** — **Fit** (scaled to fit, white borders) or **Crop** (scaled and
  cropped to fill).
- **Sleep Screen Cover Filter** — **None** (grayscale), **Contrast** (black and white) or
  **Inverted**.

---

## 9. Getting books onto the device

The simplest route is a card reader: copy files onto the SD card. On the X4 Pro the device can
be that card reader itself, over its USB cable (**USB Drive**, below). Everything else here is
wireless.

### File Transfer

**Home > File Transfer** offers four modes, and a fifth on the X4 Pro:

- **Join a Network** — connect to an existing Wi-Fi network. Each network in the list shows a
  **saved** chip when its password is stored, a padlock when it needs one, and four bars for
  its signal. A failed connection offers **Retry**, which joins the same network again with
  the same password, and **Done**, which returns to the network list. It never offers to
  forget the network, since a connection drops for reasons that have nothing to do with the
  password. To forget one deliberately, press Left on a saved network in that list.
- **Create Hotspot** — the device makes a network for your computer or phone to join.
- **Calibre Wireless** — Calibre device transfers, below.
- **Nearby Reader** — receive a file straight from another Lector, below.
- **USB Drive** (X4 Pro only) — open the SD card on a computer over the USB cable, below.

In the first two modes the device runs a web server. Open `http://<device-ip>/` in a browser,
or `http://crosspoint.local`. The web interface uploads and downloads files, manages fonts,
and edits Wi-Fi networks and OPDS servers at `http://<device-ip>/settings`. It also speaks
**WebDAV**, so the card can be mounted as a network drive.

The home page shows battery charge, free card space, and the books you have in progress.
The file manager can zip several selected files into one download, **Fetch from URL**
hands the reader a link so it downloads the file itself instead of you uploading it, and
**Send Article** turns pasted plain text, Markdown, or HTML into a single-chapter EPUB3 in the
browser and uploads it to `/articles/`.

An upload that is cut off part way through does not have to start again. What arrived is kept
beside the finished books under a `.part` name, which the library ignores, and uploading the
same file again carries on from that point. The browser checks that the part already on the
card really is the start of the file being sent, so a different file that happens to share the
name is uploaded from the beginning instead of being appended to. A file only takes its real
name, and only appears in the library, once every byte is there. Starting an upload into a
folder clears any other `.part` file left in it, so the leftovers cannot build up; you can also
delete them yourself from the file manager.

While joined to a network, the screen shows the Wi-Fi signal strength in dBm.

See [docs/webserver.md](./docs/webserver.md) for the full web server documentation, including
managing files from the command line with `curl`.

### USB Drive

On the X4 Pro the SD card can appear on a computer as a removable disk, with no card reader and
no network. The X3 and X4 do not have this mode.

1. Open **File Transfer > USB Drive**.
2. Connect the reader to the computer with a USB data cable. The screen says **USB Drive
   Connected**; the disk can take up to 30 seconds to appear on the computer.
3. Copy books, fonts or wallpapers onto the disk as you would with any USB stick.
4. Eject the disk on the computer (**Eject** in Finder, Explorer or the Linux file manager,
   or `eject /dev/sdX` in a terminal; a plain `umount` is not enough) and wait for it to
   disappear. The reader then restarts back to the File Transfer list with the new files in place.

Unplugging the cable also ends the mode, but eject first: a cable pulled while the computer is
still writing can leave a half-copied file or a damaged card, exactly as with any USB stick.

While the computer holds the card the reader does nothing else with it: no sleep, no
screenshots, no other screens, and the buttons are inactive. Before a computer connects,
**Back**, **Power** or the Home gesture leaves the mode, and it gives up by itself after five
minutes. Leaving always restarts the reader, which is how it takes the card back. **Power** and
the Home gesture restart to Home; **Back**, an eject and the time-outs restart to the File
Transfer list.

### Calibre wireless transfers

Lector works with the CrossPoint Reader device plugin for Calibre.

Installing the plugin:

1. Download the latest `crosspoint_reader` plugin from
   [the plugin releases page](https://github.com/crosspoint-reader/calibre-plugins/releases).
2. In Calibre, open **Preferences > Plugins > Load plugin from file** and pick the zip.
3. Restart Calibre.

Configuring it:

1. Open **Preferences > Plugins**, search for "crosspoint", and click **Customize plugin**.
2. Set **Host** to your device's IP address.
3. Leave the other fields alone, unless you want uploads in a subfolder: set **Upload path**
   to a path relative to the root, for example `/mybooks`.
4. Restart Calibre.

<img width="420" height="385" alt="The CrossPoint Reader plugin settings dialog in Calibre" src="https://github.com/user-attachments/assets/01fc7e33-a9a7-48ba-9e26-2e68d1f9daec" />

Sending books:

1. On the device, open **File Transfer > Calibre Wireless** and join a network.
2. In Calibre, select one or more books, right-click, then **Send to Device > Send to main
   memory**.

The plugin creates a folder for the author and copies the book into it.

<img width="783" height="310" alt="Sending books to the device from the Calibre library view" src="https://github.com/user-attachments/assets/741b0909-2e1d-4f16-8af0-2c43fbda5ce6" />

Books cannot be removed through Calibre. Delete them from the device or the web interface.

### Nearby Reader

Two Lector devices can pass a file directly, with no network in between.

1. On the receiving device, open **File Transfer > Nearby Reader**.
2. On the sending device, select the file in the browser and choose **Send to Nearby Reader**,
   or use the same row in the in-book menu's Device tab to send the open book.
3. The receiving device asks before accepting, and reports the name and size.

A device stays awake only while a transfer is running. While it is still looking for the other
reader or waiting for you to answer, **Time to Sleep** applies as usual.

A transfer that is interrupted, by walking out of range or by pressing Back, keeps what
arrived. Send the same file again and it carries on from there rather than starting over. Both
devices check the whole file against the sender's checksum before it is kept, so a resumed file
that does not match is discarded rather than left in the library. Font families are the
exception: a family that does not finish is removed whole and sent again from the start.

The list of readers found, and the accept/decline question, are chosen with the direction keys
and Select, or by tapping the row twice on a device with a touch screen (see
[Tapping](#1-buttons)).

A font family goes over the same way, from **Settings > Reader > Installed Fonts > Send font**.
The faces of the family are sent one after another and the receiving device asks once, naming
the family, how many sizes it holds and the total size. The family installs into `/.fonts/` and
is ready to pick in Text Settings straight away. A family already installed on the receiving
device is refused rather than merged or overwritten; delete it there first to replace it. A
send that stops part way leaves nothing half-installed.

Turn Wi-Fi and File Transfer off first: the radio cannot do both at once, and the device says
so if it is busy.

#### Sharing Wi-Fi and OPDS credentials

**Settings > System > Share WiFi and OPDS** sends this device's saved Wi-Fi networks and OPDS
servers to another Lector over the same Nearby radio. Open **File Transfer > Nearby Reader** on
the receiving device first, exactly as for a file. It asks before accepting, adds the networks
and servers, and reports how many of each arrived.

The bundle carries the passwords in the clear and the Nearby radio is not encrypted, so anyone
in range while it is sending can capture them. Both devices delete the bundle as soon as they
are done with it. Share when you mean to, not out of habit.

### OPDS catalogs

**Settings > System > OPDS Servers** stores up to 8 catalogs. Each entry has a name, the
catalog root URL (for a Calibre Content Server, usually ending in `/opds`), and optional
username and password. Authentication is HTTP Basic; set Calibre to Basic rather than Digest.

A catalog behind an authentication proxy such as Cloudflare Access takes up to two **Custom
Header** rows, each typed as one `Name: value` line, for example `CF-Access-Client-Id: ...`
and `CF-Access-Client-Secret: ...`. The row shows the name and hides the value, which is
stored scrambled like the password. A header named `Authorization` replaces the username and
password. Editing a saved header opens with its name alone: type a new value after it, or
leave it empty to keep the saved one; an empty line removes the header. The web page has a
name and a value box for each, and a blank value box keeps the saved value. Headers, like the password, go only to the catalog's own host, never to a
download link that points elsewhere. `Host`, `Content-Length`, `Content-Type`,
`Transfer-Encoding`, `Connection`, `Range` and `User-Agent` are refused.

A catalog that signs in with OAuth (the "password" flow of the OPDS Authentication standard,
used by some library and publisher servers) needs nothing extra: enter the username and
password as usual. When the server answers 401 with that flow, Lector posts them to the
server's own token endpoint, keeps the token it gets back (scrambled on the card, like the
password) and renews it when it expires. A token endpoint on any other host is refused. Servers
that sign in through a login web page (the "implicit" flow) are not supported.

Browse and download from **Home > OPDS Browser**. With more than one catalog saved it opens a
server picker, whose last row, **Add Server**, adds another. Back goes up one level; hold Back
for a second and let go to leave the catalog for Home. Where downloads land (**Download folder**)
and how they are named (**Filename format**: Author - Title, Title - Author, or Title) are set
from the OPDS screens and the web interface, not from the on-device Settings menu.

Catalogs can also be managed from the web interface at `http://<device-ip>/settings`.

### HTTPS servers and their certificates

An `https://` OPDS catalog or KOReader sync server is checked before your password is sent.
A server with a certificate from a public authority (Let's Encrypt, ZeroSSL, DigiCert and the
like) just connects, including after its certificate renews.

A server those cannot vouch for, such as a self-signed home server, asks **Trust this
server's certificate?** the first time, showing the start of the certificate's SHA-256
fingerprint and the server's name. Compare it with the fingerprint your server shows (for
example `openssl x509 -in cert.pem -noout -fingerprint -sha256`) and choose **Confirm** to
trust it. Lector remembers it for that server and asks again only when the certificate changes.
Trusted certificates are kept in `/.crosspoint/trusted_certs.txt` on the SD card, one server
per line; delete a line to forget one.

Plain `http://` servers are not checked, and anything typed into them, passwords included,
crosses the network readable by anyone on it.

---

## 10. Syncing reading position

### Nearby Sync

To receive a position without opening a book, choose **Nearby Sync > Receive position** on
Home. On the sending device, keep the book open and choose **Nearby Sync** from the in-book
menu's Device tab. The receiver finds its own copy and shows the matched book's name. It
keeps the furthest position, ready for the next time you open that book. No book file is sent.

Both devices must use the same **Document Matching** setting under **KOReader Sync**.
The receiver checks recent books first, then other EPUBs with existing reading caches.
It needs saved progress and a book metadata cache. Existing chapter caches must map the
position exactly; a chapter with no cache is laid out at the received text offset when you
next open the book. If the book was never opened, its metadata cache is missing, or the
position cannot be mapped exactly, it refuses and leaves the saved place unchanged. Open the book and use in-book Nearby Sync in
that case. Very large libraries can reach the search limit; the screen reports a refusal.

For the existing two-way sync, open the same book on both devices and choose **Nearby Sync**
in each in-book menu. Both keep the furthest position automatically.

Home's Nearby Sync menu also offers **Receive from Nearby Reader** for file transfers and
**Send** to browse files to send. File transfers keep duplicate files separate.

### KOReader sync

Lector syncs progress with KOReader-compatible sync servers, so KOReader apps and devices can
share a position with it.

Settings live in **Settings > System > KOReader Sync**: **Username**, **Password**, **Sync
Server URL**, **Document Matching** (Filename or Binary), **Send Document Metadata**,
**Sync Behavior**, and **Auto Sync**.

**Sync Behavior** is **Smart sync** for new configurations, which resolves the simple cases on
its own: upload when the server has nothing, do nothing when both sides agree, apply the remote
when the remote is ahead. When this device is ahead it shows both positions with **Upload
Local** selected, so the server's position is never overwritten without asking. Configurations migrated from older
firmware keep **Ask every time**, which offers **Apply Remote** and **Upload Local** on every
sync. Either can be changed at any time.

To sync, open the in-book menu and pick **Sync Progress** from the Device tab, or bind a hold
to **KOSync** in **Settings > Controls**.

**Auto Sync** (off by default) does it for you. When a book opens, the first time since the
device last slept, Lector joins a saved Wi-Fi network, fetches the position and moves to it when
the server is ahead. When the device goes to sleep after you turned pages, it sends the new
position of every book you read since it last slept (up to four) from behind the sleep screen. It uses saved networks only and never shows the network
list. Before sending, it scans once and joins only a saved network it can see, so a lock
away from every saved network costs about a second of Wi-Fi. A saved hidden network is tried
only when it was saved through **Add hidden network...** on this version or a later one:
forget one saved earlier and add it again. After a lock that reached no network, the next attempts wait 5 minutes, then
longer (up to an hour); a position that was not sent goes out the next time you read that book. If no saved network answers, the book simply opens where you left it. A server whose
certificate has not been trusted yet is skipped, so run **Sync Progress** once by hand first.
Each pull costs a few seconds of Wi-Fi and a restart back into the book.

#### Option A: the CrossPoint sync server (default)

With **Sync Server URL** left empty, Lector uses `https://sync.crosspointreader.com`, which
speaks the standard KOReader sync protocol. Positions are recorded as chapter-content offsets
and sent as the matching KOReader XPath, so devices with different fonts and layouts land on
the same text.

1. Open **Settings > System > KOReader Sync**.
2. Set **Username** and **Password** (type the plain password; the device computes the MD5
   itself). Use the same values on every device.
3. Leave **Sync Server URL** empty.
4. On the first device run **Sign Up** once. On the others run **Authenticate**.

Accounts are per server. An account on `sync.koreader.rocks` does not exist on this one.

#### Option B: BookOrbit

[BookOrbit](https://bookorbit.app) speaks the KOReader sync protocol and shares the position with
its web reader and apps, so a book continues where you stopped on either side.

1. In BookOrbit, create a KOReader sync account (its KOReader settings page) and an OPDS
   account (its OPDS settings page).
2. Add the OPDS server `https://<your-host>/api/v1/opds` with the OPDS account, and download
   books through it. A copied file is fine too, as long as it is the same file BookOrbit holds.
3. Set **Sync Server URL** to `https://<your-host>/api/v1/koreader`, set **Username** and
   **Password** to the KOReader sync account, and set **Document Matching** to **Binary**.
   BookOrbit finds the book by its contents, not its name.
4. Run **Authenticate**, then turn on **Auto Sync** if you want it.

With **Auto Sync** on, Lector also watches for books you are reading in BookOrbit but have not
put on the device. When it goes to sleep after you read, it
reads the smart shelf named **Reading** in BookOrbit's OPDS catalogue. A book counts as on the
device when the file an OPDS download would save exists, or a recent book has the same title.
When some are missing, Home shows a row such as **2 on BookOrbit, not here**. Select it to see
only those books. Confirm downloads one and opens it at your BookOrbit position. BookOrbit needs
a smart shelf called **Reading** (status is Reading) for this to work. Read-along books arrive
without their audio, so their position does not sync.

Your saved quotes go to BookOrbit as highlights each time the position syncs: from **Sync
Progress** in the reader menu, and on sleep when **Auto Sync** is on. Sending is one-way and
only adds. A quote sent twice still shows once, a highlight you delete in BookOrbit stays
deleted, and a quote deleted on the device stays in BookOrbit until you remove it there.
Quotes saved before Lector recorded where a quote sits in the book are not sent.

#### Option C: the public KOReader server

1. Set **Sync Server URL** to `https://sync.koreader.rocks`. An empty URL points at the
   CrossPoint server instead, so this one has to be typed in.
2. Set **Username** and **Password** to your existing KOReader Sync credentials.
3. Run **Authenticate**.

If you have no account yet, run **Sign Up** on the device, or register with curl:

```bash
USERNAME="user"
PASSWORD="pass"
PASSWORD_MD5="$(printf '%s' "$PASSWORD" | openssl md5 | awk '{print $2}')"

curl -i "https://sync.koreader.rocks/users/create" \
  -H "Accept: application/vnd.koreader.v1+json" \
  -H "Content-Type: application/json" \
  --data "{\"username\":\"$USERNAME\",\"password\":\"$PASSWORD_MD5\"}"
```

`HTTP 402` with `{"code":2002,"message":"Username is already registered."}` means that name is
taken.

#### Option D: your own server

1. Start a sync server:

```bash
mkdir -p kosync-quickstart
cd kosync-quickstart

cat > compose.yaml <<'YAML'
services:
  kosync:
    image: koreader/kosync:latest
    ports:
      - "7200:7200"
      - "17200:17200"
    volumes:
      - ./data/redis:/var/lib/redis
    environment:
      - ENABLE_USER_REGISTRATION=true
    restart: unless-stopped
YAML

# Docker
docker compose up -d

# Podman (alternative)
podman compose up -d
```

> [!NOTE]
> `ENABLE_USER_REGISTRATION=true` is convenient for the first setup. Once your users exist,
> set it to `false` or remove it, so nobody else can register.

2. Verify the server:

```bash
curl -H "Accept: application/vnd.koreader.v1+json" "http://<server-ip>:17200/healthcheck"
# Expected: {"state":"OK"}
```

3. Register a user once. The server authenticates on the MD5 of the password, so register
   with that MD5:

> [!WARNING]
> Sending a reusable MD5-derived password over plain HTTP is insecure.
> Create unique sync-only credentials and do not reuse main account passwords.
> Prefer `https://<server-ip>:7200` whenever traffic leaves a fully trusted LAN, or when using
> untrusted networks.
> Use `curl -k` only for self-signed certificate testing.

```bash
USERNAME="user"
PASSWORD="pass"
PASSWORD_MD5="$(printf '%s' "$PASSWORD" | openssl md5 | awk '{print $2}')"

curl -i "http://<server-ip>:17200/users/create" \
  -H "Accept: application/vnd.koreader.v1+json" \
  -H "Content-Type: application/json" \
  --data "{\"username\":\"$USERNAME\",\"password\":\"$PASSWORD_MD5\"}"
```

4. On each device, open **Settings > System > KOReader Sync**, set **Username** and
   **Password**, set **Sync Server URL** to `http://<server-ip>:17200`, and run
   **Authenticate**. For the HTTPS listener use `https://<server-ip>:7200`.

---

## 11. Fonts and dictionaries

### Fonts

Lector ships with ChareInk, in regular, bold, italic and bold italic, at 12, 14 and 16 pt. Fonts on
the SD card are added to that list, and can bring other sizes and scripts the built-in font does
not cover, including Chinese, Japanese and Korean.

Three ways to install one:

1. **From the device:** **Settings > Reader > Manage Fonts**, then pick a family to download
   over Wi-Fi. A file that fails or arrives damaged is fetched again, up to five attempts,
   and the screen says which attempt is running. A transfer cut short carries on from where
   it stopped rather than starting the file over, and a dropped Wi-Fi connection is waited
   out for up to 20 seconds per attempt. Files that did arrive are kept, so starting the
   download again fetches only what is still missing, a few styles at a time on a poor
   connection. **Download all** and **Update all** carry on past a family that will not
   install and name the ones that failed at the end. A file is only put in place once it
   matches the checksum published for it, so an update that fails leaves the copy already
   on the card untouched.

   When it does not work, the screen names the reason rather than saying only that it
   failed: no Wi-Fi connection, could not reach the font server, the font server refused
   the request, the download stopped before it finished, could not write to the SD card,
   not enough memory, or a font list this firmware cannot read. Under that sits a short
   code line such as `E4 no reply heap 28112/9216`, which grows an `oom` section when the
   reader ran out of memory rather than out of network. That line is diagnostic, not
   something to act on; photograph it if you report the problem.
2. **From the web interface:** in File Transfer mode, open the **Fonts** page and upload
   `.cpfont` files. On the X4 Pro the page also takes `.ttf` and `.otf` files (2 MB at most
   per file); the X4 and X3 refuse them.
3. **From your computer:** copy fonts into `/.fonts/` (preferred) or `/fonts/` on the card.
   Files come from the [lector-fonts repository](https://github.com/diogo7dias/lector-fonts).

Downloadable families are built at point sizes 10 through 18, so a family gives you nine
reading sizes. OpenDyslexic is the exception, at 8 through 16, because its glyphs run larger
for the same point size.

On the X4 Pro a family can also be an ordinary TrueType or OpenType font. Make a folder under
`/fonts/` (or `/.fonts/`) named after the family and copy its `.ttf` or `.otf` files into it,
one per style, or upload them from the **Fonts** page of the web interface. The style comes
from the file name (`Regular`, `Bold`, `Italic`, `BoldItalic` and the usual weight names); a
folder holding a single file uses it as the regular face. Such a family offers every size from
8 to 40 pt. The X4 and X3 skip these
folders; they read `.cpfont` families only.

Installed families appear in **Settings > Reader > Text Settings > Reader Font Family**. Full
details are in [docs/sd-card-fonts.md](./docs/sd-card-fonts.md).

**Settings > Reader > Installed Fonts** lists what is actually on the card, whichever way it
got there, and needs no Wi-Fi. Each row shows the family, how many sizes it holds and how much
space it takes, and the family the reader is currently set to is marked. Pick a family for two
choices:

- **Send font** — pass the whole family to another Lector nearby, described below.
- **Delete** — remove it from the card, after a confirmation. Deleting the family in use puts
  the reader back on a built-in font.

### Dictionaries

Lector reads StarDict dictionaries from `/dictionaries/<name>/` on the card. Pick one in
**Settings > Reader > Dictionary**; the row only appears once a dictionary folder exists.

Look a word up from the in-book menu's **Look Up** row, or bind a hold to **Dictionary** in
**Settings > Controls**. On a touch screen, hold the word itself for half a second and lift:
its definition opens straight away, and Back returns to the page. To look up a phrase, hold
its first word, drag to its last, and tap **Look Up** (see **Quotes**). A long definition turns pages
with the same taps and side swipes as a book page, under **Touch Reader Controls**. A word with several entries in
the dictionary shows all of them, each under its own headword. Setup and usage
are covered in [docs/dictionary.md](./docs/dictionary.md).

---

## 12. Settings reference

Settings opens on five cards: **Display**, **Reader**, **Controls**, **System** and **File
Transfer**. Under each name a line shows how that part is set up now, for example
`Refresh 15 pages · Sleep: Cover` on Display or `ChareInk 14 · Ruled OFF` on Reader. Open a
card to see its rows. They are grouped under black section headings such as **Text**, **Page**
or **Library**. The buttons step over the headings, so every press lands on a row.

Step through a list a row at a time. Hold the same button to scroll: it moves a row at a
time, speeds up to five rows at a time after about a second, and stops at the end of the list.

Paths in this reference, such as **Settings > Reader > Dictionary**, name the card a row
belongs to, so the rows here appear in the same order as on the device.

### 12.1 Display

**Selection Style**

- **Selection Style** — how the highlighted row is marked in every menu, list and popup.
  **Solid** fills the row black and turns its text white (the original look). **Brackets**
  puts corner marks around the row's own text, hugging the label and its value
  separately, and leaves the text black.
  **Caret** puts an arrow at the left of the row, beside its first line of text, and leaves the
  text black. Brackets and Caret redraw faster and ghost less on e-ink, because they leave most
  of the row untouched.

**Sleep screen**

- **Sleep Screen** — Dark, Light, Custom, Cover, Cover + Custom, Quick Resume, Transparent, Cover + Transparent, Quote, None. See [section 8](#8-sleep-screen-and-wallpapers).
- **Fast Unlock** — shorter recovery-chord wait at wake, and the page painted over the sleep screen in one pass. Off if a wake lands in the recovery picker by itself or the wallpaper ghosts into the first page.
- **Sleep Footer Text** — your own line of text on the sleep screen.

**Wallpaper**

- **Sleep Screen Cover Mode** — Fit or Crop.
- **Sleep Screen Cover Filter** — None, Contrast or Inverted.
- **Sleep Image Quality** — Fast or Pretty.
- **Show Wallpaper Name**, **Show Wallpaper Position** — what is drawn over the wallpaper. Both off by default.
- **Shuffle Wallpapers** — reshuffle the rotation now.

**Quick Panel**

The quick panel is a band over the top of the screen. Open it by swiping down from the top
edge, or with a button bound to **Quick Panel** (on the X3 and X4, a double click of **Power**
out of the box); the same gesture, **Back** or **Confirm** on a row put it away, and the
X4 Pro's Home key closes it and goes Home. It holds a
grid of actions for where you are (Refresh, Rotate, Continue Reading, Random Book, Search and
Settings outside a book), a size or sort row, and the battery and free space. **Volume Up** and
**Volume Down** move the cursor down the rows and on through the grid, **Left** and **Right**
step a row or move along the grid, and **Confirm** runs the highlighted button.

**Frontlight** (boards with a frontlight)

The frontlight has no rows in Settings. The quick panel adds its rows on these boards: on or
off, brightness and warmth. **Toggle Frontlight**, a
button action, flips it without opening the panel. The other frontlight options live on the web
settings page only: **Max Brightness** (the top of the panel's brightness track, 10 to 100,
default 30; a brightness above it is brought down), **Restore on Wake** (on by default), and **Light Schedule**
(at each wake inside the set hours the light comes on by itself) with its **Light On From** and
**Light On Until** times (20:00 and 07:00 by default, in quarter-hour steps).

In a book the quick panel also has **Calm Mode**, **Align Left** and **Justify**, which set the
book's paragraph alignment for that book alone, and **Paragraph Spacing**, which turns the extra
space between an EPUB's paragraphs on or off for that book. **Calm Mode** (off by default; also
bindable to a button in **Settings > Controls > Buttons**) hides the status bar, the hidden-bar
progress bar, the low-battery notice (until the critical 5% floor) and the centre-tap reader menu;
a bottom-edge swipe up and **Confirm** still open the menu. Turning it off restores your exact
previous setup without modifying saved status bar settings or Reading Themes.

**Screen**

- **Refresh Frequency** — a full refresh every 1, 5, 10, 15 or 30 pages, or **Never**. Never
  turns off the page-counted refresh only; the reader still cleans the panel when enough ink
  has moved to need it, so a long session does not end up smeared.
- **Refresh at Chapter Start** — off by default. On adds a full refresh on the first page of
  each chapter you enter, on top of Refresh Frequency, so every chapter opens clean. EPUB only.
- **Chapter Wipe** — off by default. On shows a crosshatch frame for one refresh (about half a
  second) before the first page of each chapter you turn into. Not on opening a book. EPUB only.
- **Screen Calibration** — if the bezel covers the edge of the page, or leaves a gap, move each
  edge until the whole border shows: pick **Top**, **Right**, **Bottom** or **Left**, then drag the
  slider or step it, from 0 to 32 pixels. **Save** applies it to reading pages and the status
  bar, and each book lays itself out again the next time you open it; Back keeps what you had.
  **Reset to Defaults** goes back to this model's measurements. It is kept on the device, not on
  the card. Menu screens keep their own spacing.
- **Sunlight Fading Fix** — software fix for white X4 units fading in direct sunlight. Not shown on the X4 Pro.
- **Hide Battery %** — Never (default), In Reader or Always. The battery icon stays; In Reader
  drops the number from the reader's status bar, Always from the screen headers too.

**Home**

- **Author On Home** — Initials or Full Name.

### 12.2 Reader

**Text** — **Text Settings** (below), **Manage Fonts**, **Installed Fonts**, **Dictionary**.

**Text Settings** is one scrolling list, banded into **Type**, **Spacing**, **Margins** and
**Reading Aids**, under a live preview. The preview is a split slice of a real page: the top
of the page, a dashed cut, then the bottom of the page, so both vertical margins and the
status bar are on screen at their true size while you tune them. Numeric rows (line spacing, the
spacings, indentation and the margins) are adjusted on the same screen: Select, or a tap on
the row, docks an adjuster right under the preview, with the setting's name and value, a
track, and a minus and a plus. Touch anywhere along the track to jump to that value, or drag
along it and the value follows your finger. Left and Right (or the minus and the plus) move
one step, and the side buttons move the setting's larger step. The preview reflows with every
step, so nothing covers the page you are judging. **Done** (Back or Select) puts the adjuster
away and saves the value once; a tap on another row does the same and then opens that row.
The other numbers in Settings, such as Time to Sleep, open their own slider screen.

- **Font** — ChareInk, plus any SD card families; opens a full-screen picker.
- **Size** — the sizes the chosen font has: 12, 14 and 16 pt for ChareInk, whatever is installed for
  an SD card family.
- **Ink Weight** — finer font weight in five steps, from -2 (lighter) to +2 (darker); 0 is the font
  as designed (default). On the X4 Pro, TrueType and OpenType fonts shift their rasterizer coverage
  threshold; built-in and `.cpfont` fonts adjust their 2-bit ink levels before Paperback Look. Changing
  it repaints immediately without re-paginating the book.
- **Line Spacing** — 80% to 175%. Full text pages automatically share leftover vertical space above
  and below the text, inside your margins and status bar. Short pages stay at the top;
  EPUB pages with images, rules or ruby keep their layout. This balances line boxes
  (within one pixel), so letter shapes may still look slightly uneven.
- **Horizontal Margin** — left and right, one shared value, 5 to 40 in steps of 5. Listed as
  **Margin** in All Sides, where it is every side. A saved margin outside that range or off the
  step comes back to the nearest step.
- **Link Margins** — **Off**, **Top+Bottom** or **All Sides**. Off lists **Top Margin** and
  **Bottom Margin** separately; Top+Bottom replaces them with a single **Vertical Margin**;
  All Sides puts one number on all four sides and leaves **Margin** as the only margin row.
  All Sides also turns **Dynamic Margins** off and stops offering it, because Dynamic
  Margins works out a left and right margin of its own that the other two sides cannot
  match. Switching back does not turn it on again. New devices start at All Sides with 5
  on every side; devices updating from an older build keep the margins and the link they
  had, and anyone still on the old **Uniform Margins** switch comes back to All Sides.
- **Dynamic Margins** — Off, Auto (min 10px) or Auto (min 20px). Not listed in All Sides.
- **Paragraph Indentation** — the first line's indent in spaces of the reading font, 0 to 5
  (2 by default). The book's own indent is not used; the one exception is a negative indent
  (a hanging first line), which the book keeps. Word Spacing scales the indent with the gaps.
  Settings from an older build keep their look: Book becomes 2, a percent becomes the nearest
  whole number of spaces (about one space per 10%, from 1 to 5).
- **Paragraph Alignment** — Justify, Left, Center, Right or Book's Style.
  Justify distributes all spare horizontal space between eligible gaps; the last line
  keeps its natural spacing. Korean text wraps and stretches only at the spaces between
  words, never between syllables, unless a single word is wider than the line.
- **Embedded Style** — **Off**, **Text Only** or **Full** (default).
  - **Full** honours all supported CSS: text styling (bold, italic, underline, small caps,
    superscript and subscript, writing direction, preserved whitespace, hidden passages) and layout
    (alignment, margins, padding, font sizes, image sizes, borders, shading, drop caps and page
    breaks). **Paragraph Alignment: Book's Style** needs Full.
  - **Text Only** keeps inline text styling (weight, italic, decoration, small caps, superscript
    and subscript, white-space, `display:none` and writing direction) while dropping the book's
    margins, padding, font sizes and boxes so your reader layout stays uniform.
  - **Off** ignores the book's stylesheet completely. Bold and italic written as `<b>` or `<i>` tags
    are always honoured, and `<code>`, `<pre>`, `<tt>` and `<kbd>` render in the installed
    **IBMPlexMono** SD font when present (falling back cleanly to the reading font if missing).
  Numbered lists always show their numbers, and a nested list counts from 1 again.
  An inline passage in another writing direction keeps the enclosing paragraph's base direction.
  Arabic and Persian text is shaped once, preserving Alef and Lam order; Latin ligatures remain supported.
  Each scaled font size is drawn at the nearest size the font has: 12, 14 or 16 pt for ChareInk,
  whatever is installed for an SD card family. The book's fonts and line height are never used: the
  reader's own settings always win. Footnote and cross-reference links are always underlined, and
  kerning and ligatures are always on.

By default pages end full: no line moves to the next page to keep a paragraph's first or last line
or a heading from sitting alone at a page edge. Page breaks the book asks for still apply. Tables size each column to its content.
A table too wide for the page sets its cell text one size smaller, a cell that spans columns
stays in the grid, and a header row gets a heavier rule under it. A table keeps its grid up to
4 columns (6 on the X4 Pro); wider ones list their cells one under another.
Small pictures in the text (icons, symbols drawn as images, up to two lines tall) stay inside
the line; larger ones get their own centred block, and a small picture alone in its paragraph
is centred.
- **Keep Headings** — off by default. When on, a heading never sits stranded at the bottom of a
  page (it moves to the next page with the text that follows it), and paragraphs of four or more
  lines avoid leaving a single orphan line at the bottom or a single widow line at the top of the
  next page. Changing it re-paginates the book.
- **Bionic Reading** — bolds the first part of each word as a fixation point.
- **Word Spacing %** — sets the baseline gap between words, from 50% to 200% in steps of 25.
  100% keeps the original spacing. Justified lines still stretch to the right margin;
  this does not limit their gaps. With Guide Dots, it adjusts the spaces on both sides
  of each dot, keeping the dot the same size. Changing it re-paginates the book.
- **Character Spacing** — the space between letters, from -2 (tighter) to +2 (looser) in
  whole pixels. 0 is the font as designed. Changing it re-paginates the book. Settings from an
  older build, which counted half pixels, come back to the nearest whole pixel.
- **Images** — **Display** (default) draws pictures (JPEG, PNG and GIF) in grey, **Placeholder**
  keeps only the picture's alt text, **Suppress** hides them, and **Black & White** draws them as
  a black and white dither, so a page with a picture turns as fast as a text page and skips the
  extra grey refresh. A saved Placeholder or Suppress from an older build, when the row was
  gone and the setting did nothing, comes back as Display.
- **Guide Dots** — draws a middle dot in a widened gap between words.
- **Hidden Dots** — only listed while Guide Dots is on: keeps the widened gaps and draws no dot in them.
- **Extra Paragraph Spacing** — space between paragraphs instead of a first-line indent.
  When a nested block ends, the enclosing block's top and bottom spacing is not added again.
- **Debug Layout Borders** — draws the layout boxes; a diagnostic, not a reading feature.

**Page**

- **Reading Orientation** — Portrait, Landscape CW, Portrait 180°, Landscape CCW.
- **Paragraph Numbers** — Off or Per Chapter, with **Number Size**: Small, Medium or Large,
  set in crisp pixel digits (8, 16 or 24 px tall), and **Number Position**: By the Text
  (default) tucks each number into the indent just before the paragraph's first letter;
  Page Edge lines them up in a column at the left edge. Either way they sit centred on
  the first line, and a number is left out where there is no room for it.

**Look**

- **Paperback Look**, **Paperback Status Bar**. Paperback Look thickens the reading text like
  heavier paperback ink: **ON** adds one pixel, **BOLDER** a second pixel to vertical strokes.
  The in-book menu row cycles OFF, ON, BOLDER.
- **Ruled Lines** — a dotted rule under every line of text, like notebook paper: OFF (default),
  Light, Medium or Dark. EPUB and TXT books (not XTC). The rule breaks around descenders and
  accents, and it does not change where the text sits, so pages keep their layout. Text
  Settings' preview and the quote and word selection screens show the page without it.
- **Night mode** — inverts the screen.
- **Reader Menu Style** — **List** (the tabbed menu, the default) or **Toolbar**, see
  [section 6](#6-the-in-book-menu).
- **Customise Status Bar** — see below.

**Library**

- **All Bookmarks** — every book's bookmarks in one list, book by book. Pick one to open
  that book at it.

**Customise Status Bar** places each element in one of six slots (top left, top centre, top
right, bottom left, bottom centre, bottom right) or turns it off: **Battery**, **Clock**,
**Chapter Title**, **Book Title**, **Page in Chapter**, **Book %**, **Chapter %**, **Chapter Number**, **Pages This
Session**, **Pages to Paragraph**. Alongside them:

- **Chapter Title** and **Book Title** can go in different slots, for example the book on top
  and the chapter at the bottom. Put both on the same edge (both top or both bottom) and they
  stack in the Chapter Title's slot: the book title on the first line, the chapter title on
  the line below. A book without chapters (TXT) shows
  its title in the Book Title slot, or in the Chapter Title slot when Book Title is off.
- **Truncate Title** — off, a title wraps onto as many lines as it needs; on, it is cut short
  with `…`. A Book Title in a slot of its own is always one line.
- **Page Format** — `N/M` or `N left`. In a book that carries the printed page numbers the
  printed page follows it, `8/12 p.17`.
- **Pages This Session** counts forward page turns in the open EPUB or TXT book and resets when
  the book closes.
- **Pages to Paragraph** shows `>P.0` on most pages: the paragraph you are in ends before the
  next one starts. `>P.2` means it runs two more pages. In a TXT book each line break starts a
  paragraph, so a file broken into short lines always shows `>P.0`.
- **Book Bar** and **Chapter Bar** — Off, Top or Bottom, with **Bar Thickness** (Slim 3 px,
  Medium 6 px, Fat 9 px). On EPUB books the Book Bar has a small notch where each chapter
  starts. A book with more than 64 chapters shows a plain bar.
- **Progress Bar** — Off, Slim, Medium or Fat, for when the status bar itself is off.
- **XTC Status Bar** — Hide, Bottom or Top, for XTC books.
- **Clock Format** (24-hour or 12-hour) and **Clock Synced**.
- **Time Zone** and **Daylight Saving** are in Settings > System, and on the web settings page.
  The X4 has no clock chip, so it does not show them.
  Every clock follows them: the status bar and Home clock, the Light Schedule and the dates in
  your reading history.

### 12.3 Controls

**Buttons**

- **Remap Front Buttons** — reassign each bottom-edge button (X3/X4 only; unavailable on X4 Pro).
- **Orient front buttons** — rotate their meaning with the screen.
- **Side Button Layout (reader)** — Prev/Next, Next/Prev, Disabled, Next/Next or Prev/Prev.

**Power button**

- **Short Power Button Click** — Ignore, Sleep, Page Turn, Refresh Screen or Footnotes.
- **Wake Hold** — Normal or Fast, for the hold that wakes the device from sleep.
- **Quick-return from footnotes**.
- **Double-click power** — bind an action to two quick presses. While something is bound
  here, **Short Power Button Click > Sleep** stops sleeping on a single press, since that
  press is the first half of the double click.
  Most bindings act through the in-book menu, which only EPUB books have. In a `.txt` book
  the double click runs **Status Bar** and **Hold Wallpaper**; in an `.xtc` book, **Hold
  Wallpaper**. Bound to anything else, it stays inactive there and the power button keeps
  its normal, undelayed behaviour.
  A single power click waits about a quarter of a second while the reader watches for a
  second one, which is noticeable when Power also turns the page. That wait is only paid
  while the bound action can actually run: with **Footnotes** bound, a page without
  footnotes turns instantly, and a page with them does not.

**Hold**

- **Long press while reading** — the action bound to holding Confirm in a book.
- **Hold in in-book menu** — the action bound to holding Confirm with the menu open.
- **Pop-up Items** — which rows the pop-up contains.

**Back**

- **Short Back to File Browser** — swap the short and long press of Back in a book. Not shown on the X4 Pro.
- **Home Back Button** — None, Resume (default) or Sortes.

### 12.4 System

**Power** — **Time to Sleep**: 1 to 30 minutes, or Never. Default 10 minutes.

**Library**

- **Show Hidden Files**.
- **File Browser Order** — Alphabetical, Random, Recently Added or Last Read.
- **Open Book on Boot** — Last Book or Random Book.
- **Clear Read Books from Recent List**.
- **Move Finished Books to Read Folder**.
- **Move Opened Books to Recents Folder**.

**Network** — **Wi-Fi Networks**, **KOReader Sync**, **OPDS Servers**, **Share WiFi and OPDS**.

**Device**

The menus are in English only; there is no Language row. Books in any language still open and
render as before, right-to-left ones included, and the on-screen keyboard is QWERTY.

- **Time Zone** — a list of 104 zones, west to east, each with its standard offset: cities
  and regions that keep their own summer time (London, Berlin, New York, Sydney and so on), then
  places that do not, then a plain **UTC-12:00** to **UTC+14:00** at the end for anywhere the list
  misses. The web settings page offers the same list as a drop-down. Until you pick one the clock
  shows UTC.
- **Daylight Saving** — **Auto** follows the zone's own rule. **On** and **Off** override it with a
  fixed offset (the standard one, plus an hour when On), for a country that changed its law
  before this firmware did.
- **Device Name** — how this reader introduces itself to another during Nearby Sync. Left
  empty, it falls back to a generated `Lector-XXXX` name derived from the MAC address.
- **Maintenance** — the rows you need a few times a year, one level down. Back from it
  returns to the Maintenance row.
- **Maintenance > Clean Up Storage** — removes cached data for books that are no longer on
  the card. Books keep their progress.
- **Maintenance > Clear Reading Cache**.
- **Maintenance > Check for updates**, **SD Card Firmware Update** and **Install Other
  Firmware** — see [section 13](#13-updating-the-firmware).

---

## 13. Updating the firmware

Three routes:

- **Over Wi-Fi:** **Settings > System > Maintenance > Check for updates**. Only stable releases are
  offered, never experimental ones. A device already running an experimental build is
  offered the next stable release, which is how it gets back onto a stable line without a
  cable.
- **From the SD card:** put a firmware `.bin` on the card and use **Settings > System > Maintenance > SD
  Card Firmware Update**, or open the `.bin` from the file browser.
- **Over USB:** the [Lector flasher](https://diogo7dias.github.io/lector-web/)
  installs over Web Serial from a browser.

A firmware written from the SD card is read back off the flash and compared against the file
before the device is told to boot it, so a bad write is reported as **Firmware check failed,
nothing changed** instead of restarting into the old firmware as though it had worked.

Every Lector release is signed. **Check for updates** installs only a signed release and
refuses anything else with **Update not signed by lector**, so a tampered download can never
become the firmware you boot. The SD card still takes any firmware, since other firmwares
are not signed by Lector: an unsigned file asks **Not signed by lector. Install?** before it
installs. A file that carries a Lector signature that does not match is refused as
**Invalid firmware file**.

If the device will not boot far enough to reach Settings, hold **Volume Up** together with
**Power** at boot (**Volume Down** on the X4 Pro, whose Volume Up pin doubles as the chip's
boot-strap pin). That goes straight to the SD card firmware update screen. If no card is
mounted, the screen asks for one and keeps trying, so a card swapped in at that point still
gets you there.

### Leaving Lector

Some Xteink X3 and X4 units are sold with USB flashing disabled in the factory, mostly ones
bought through Chinese retailers. On a locked unit there is no serial fallback, so these are
the ways off this firmware, safest first:

1. **SD card.** Put the other firmware's `.bin` on the card and use **Settings > System > Maintenance > SD
   Card Firmware Update**. Stock, CrossPoint, CrossInk and INX images all install this way.
   Lector points the bootloader at the new image with rollback left disabled, so the new
   firmware stays after its first boot rather than being sent back here.
2. **Recovery chord.** Same thing when Lector will not start: **Volume Up + Power** at boot
   (**Volume Down + Power** on the X4 Pro).
3. **Over Wi-Fi, with the OTA Unlocker.** The
   [CrossPoint OTA Unlocker](https://crosspointreader.com/#unlock-tool) turns a computer into
   a Wi-Fi hotspot that answers the reader's own update check with a firmware you choose. On
   the reader, use **Settings > System > Maintenance > Install Other Firmware**: unlike **Check for
   updates**, it installs whatever the server offers, including the same version, an older
   one, or a different firmware entirely.
4. **Last resort:** an SPI flash clip, wiring straight to the flash chip. See
   [docs/fix-bricked-xteink.md](docs/fix-bricked-xteink.md).

The X4 Pro is not supported by the OTA Unlocker; use routes 1 and 2 there.

---

## 14. Troubleshooting

**The device is stuck in a bootloop.** Press and release **Reset**, then hold **Back** and
**Power** to boot to the Home screen. If that does not work, hold **Volume Up** and **Power**
at boot (**Volume Down** on the X4 Pro) to reach the SD card firmware update screen.

**This device cannot be flashed over USB.** Some units ship with USB flashing disabled from
the factory. See *Leaving Lector* in [section 13](#13-updating-the-firmware) for the routes
that still work.

**Something is broken after a settings or cache change.** Delete the `.crosspoint` folder on
the card, or just the parts of it that matter: `settings.json`, `state.json`, or the `epub_*`
cache folders.

**A crash happened.** Lector writes a crash report to the SD card without needing a USB
connection. Attach that file to any bug report.

**More detail is needed.** Connect the device over USB and run the debugging monitor (needs
Python 3 with `pyserial`, `colorama` and `matplotlib`; install with
`pip3 install pyserial colorama matplotlib`):

```
python3 scripts/debugging_monitor.py
```

It finds the serial port on its own, or takes one:

```
python3 scripts/debugging_monitor.py /dev/ttyACM0        # Linux
python3 scripts/debugging_monitor.py /dev/tty.usbmodem1  # macOS
python3 scripts/debugging_monitor.py COM7                # Windows
```

It colour-codes the log by category, graphs free memory once a second, sends commands you type
back to the device, and saves screenshots the device triggers to `screenshot.bmp`.

| Option               | Description                                               |
| -------------------- | --------------------------------------------------------- |
| `--baud RATE`        | Baud rate (default: 115200)                               |
| `--filter KEYWORD`   | Show only lines containing the keyword (case-insensitive) |
| `--suppress KEYWORD` | Hide lines containing the keyword (case-insensitive)      |

```
# Show only memory-related log lines
python3 scripts/debugging_monitor.py --filter MEM

# Hide noisy SD card log lines
python3 scripts/debugging_monitor.py --suppress "[SD]"
```

Press **Ctrl-C** or close the graph window to exit.

### Known limits

- **Large cover images** slow the first open: a cover around 2000 pixels tall takes several
  seconds to convert for the sleep screen and the home thumbnail.
- **Interlaced GIFs and progressive JPEGs** in EPUBs are not rendered, and fall back to an
  `[Image]` placeholder. Ordinary JPEG and PNG images render, and so do GIFs: a GIF, whether
  in the text or as the book's cover, shows its first frame only, up to 3072 pixels wide.

Bugs and logs are welcome as issue tickets.
