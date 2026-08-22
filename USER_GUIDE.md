<!-- lector-version: 0.26.3 -->

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
- [7. Bookmarks, quotes and reading stats](#7-bookmarks-quotes-and-reading-stats)
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

The four bottom-edge buttons can be reassigned in **Settings > Controls > Remap Front Buttons**,
and the side buttons can be swapped or disabled with **Side Button Layout (reader)**.

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

Boot, the plain sleep screen and the unlock screen all show one of six engraved crests
between the two banners. The sleep screen picks one at random, and waking redraws that same
crest rather than swapping it under you.

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

**Wake Straight to Book** (Display settings) takes a wake from sleep back into the book
without stopping at any other screen.

### First launch

The first boot lands on the [Home screen](#3-home-screen). Later boots reopen the book you
were reading. **Open Book on Boot** changes that: **Last Book** opens the last-read book
every time, even after the reader was closed, and **Random Book** opens one of the books in
progress at random. Holding Back during boot skips both and lands on Home.

On a fresh card, Lector creates the folders it uses: `/read`, `/recents`, `/sleep` and
`/sleep pause` (that last name contains a space).

---

## 3. Home screen

The Home screen is the entry point. It lists the books you have been reading, and below them:

- **Continue Reading** — reopen the most recent book. Present in themes that put it in the
  menu, and only once something has been read.
- **Browse Files** — the file browser.
- **OPDS Browser** — shown once at least one catalog is configured, see
  [section 9](#9-getting-books-onto-the-device).
- **File Transfer** — Wi-Fi transfers, see [section 9](#9-getting-books-onto-the-device).
- **Settings** — see [section 12](#12-settings-reference).

**Settings > Controls > Home Back Button** decides what **Back** does here: nothing, resume
the current book, or open [Reading Stats](#7-bookmarks-quotes-and-reading-stats).

**Settings > Display > Author On Home** switches the author line between initials and the
full name.

---

## 4. Browsing files

The file browser shows the current folder path at the top, files with their extensions, and
folders in brackets, for example `[folder-name]`.

- **Move the cursor** — **Left** (or **Volume Up**) and **Right** (or **Volume Down**). Hold
  either one to move a whole page at a time.
- **Open** — **Confirm** opens the folder, book or image under the cursor.
- **File actions** — hold **Confirm** on an entry. A file another reader can accept opens a
  small menu offering **Send to Nearby Reader** and **Delete**; anything else goes straight to
  the delete confirmation. Nothing is deleted without confirming.
- **Search** — search inside the current folder, with the results ranked by how well they
  match.

Renaming and moving files is done from the web interface rather than on the device, see
[section 9](#9-getting-books-onto-the-device).

Each row shows its file type on the right, and a book you have opened before also shows how
far in you are on the left, in front of the title: a percentage while you are reading it, or
**Read** once it is finished, in the same style the home screen uses. The
badge stays even after the book leaves the Recents list or is filed into `/read`. Books you
have never opened show no badge, and comics (XTC) never carry one.

Upgrading from an older release, the books still in your Recents list get their badge on the
first boot; any other book read long ago gets one the next time you close it.

**Settings > System > Show Hidden Files** decides whether entries beginning with `.` appear.
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
| `.txt`          | Plain-text reader                                     |
| `.xtc`          | XTC reader, the Xteink book format                    |
| `.bmp`          | Image viewer                                          |
| `.pxc`          | Image viewer for the packed wallpaper format          |
| `.bin`          | Offered as a firmware file, see [section 13](#13-updating-the-firmware) |

From the image viewers you can set the picture as your sleep wallpaper.

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

**Settings > Controls > Side Button Layout (reader)** swaps the two side buttons or disables
them while reading. **Orient front buttons** turns the bottom-edge buttons along with the
page when you read in landscape.

With **Short Power Button Click** set to **Page Turn**, a short press of **Power** also turns
the page.

### Chapters

Chapter jumps live in the in-book menu, under **Navigate > Select Chapter**. Holding a page
turn button does nothing: page turning fires on the press, so the page never waits to find out
whether a hold was coming.

### Footnotes

In an EPUB with footnotes, open a footnote from its reference and return afterwards to where
you were. Falling asleep or closing the book while a footnote is open still reopens the book
at your reading position, not at the footnote.

**Short Power Button Click > Footnotes** opens the footnotes for the page with a short press;
when the page holds a single footnote, it opens that one directly.
**Quick-return from footnotes** makes a short power press act as **Back** while a footnote is
on screen.

### Leaving the book

- **Back** closes the book and returns Home.
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

**Pop-up Items** decides which rows the quick pop-up those bindings can open actually
contains.

Text Settings is no longer one of the actions on offer, and is no longer a pop-up row.
It is reached from Settings, or from the in-book menu. A binding that pointed at it
reads as Disabled after the update; pick another action for that gesture.

### Reading in landscape

**Settings > Reader > Reading Orientation** offers Portrait, Landscape CW, Portrait 180° and
Landscape CCW. The in-book menu can rotate the screen without leaving the book.

---

## 6. The in-book menu

Press **Confirm** while reading. The menu opens in tabs; **Left** and **Right** move within a
tab, and the tab strip runs across the top. Rows that do not apply to the open book are not
shown at all.

The menu opens on **Navigate**. **Settings > Reader > Book Menu Opens On** changes that to
**Book**, **Look** or **Device** instead, and that choice always wins. The **Sleep Screen**
tab appears alongside the others while the lock screen has a wallpaper to act on; step to it
with the tab strip.

### Navigate

- **Select Chapter** — the table of contents. Chapters the book lists but cannot actually
  open are left out, so every row here goes somewhere.
- **Go to %** — jump by percentage.
- **Go to Paragraph** — jump by paragraph number, shown only while paragraph numbering is on.
- **Bookmarks** — the list, shown once the book has one.
- **Toggle Bookmark** — drop or lift a bookmark on this page.
- **Footnotes** — shown only in books that have them.

### This Book

- **Reading Stats** — this book's numbers.
- **Look Up** — pick a word on the page and read its definition. Needs a dictionary, see
  [section 11](#11-fonts-and-dictionaries).
- **Lookup History** — the last 100 words you looked up, newest first. Pick one to read its
  definition again.
- **Grab Quote** — save a passage from this page.
- **View Quotes** — read back what you saved, shown once the book has quotes.
- **Remove This Book** — unfile the book from recents; the file stays on the card.
- **Delete Book** — erase the file, after a confirmation.

### Look

- **Reader Settings** — text settings for this book alone.
- **Reset Reader Settings** — drop the per-book override, shown only when one exists.
- **Steal Look** — copy another book's look onto this one, once.
- **Reading Themes** — apply a saved look. Up to 8 themes are kept.
- **Paragraph Numbers**, **Number Size**, **Paperback Look**, **Paperback Status**.
- **Status Bar** — show or hide it. With the bar hidden, a **Progress Bar** row appears in its
  place.
- **Customise Status Bar** — everything the bar can show and where, for this book.
- **Orientation** — rotate the screen.

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

Press **Back** to close the menu.

---

## 7. Bookmarks, quotes and reading stats

### Bookmarks

Bind **Long press while reading** to **Bookmark** and hold **Confirm** to drop one, or use
**Toggle Bookmark** in the menu. Open the list from **Navigate > Bookmarks**, press
**Confirm** on an entry to jump there. To delete one, hold **Confirm** on it, then confirm.

Bookmarks are stored as JSON under `/.crosspoint/bookmarks`.

### Quotes

**Grab Quote** saves a passage from the current page. Quotes for a book live beside it on the
card as a plain-text sidecar named after the book, for example `Book_QUOTES.txt`, with the
chapter and anchor recorded next to each quote. **View Quotes** reads them back on the
device.

### Reading stats

With **Settings > System > Track Reading Stats** on, Lector records reading time and pages.
**Reading Idle Limit** sets how long a page can sit untouched before that time stops counting.
Stats are visible from the Home screen and per book from the in-book menu, and the sleep
screen can show a stats dashboard.

---

## 8. Sleep screen and wallpapers

**Settings > Display > Sleep Screen** decides what the panel shows while the device sleeps:

| Mode           | Behaviour                                                                        |
| -------------- | -------------------------------------------------------------------------------- |
| Light          | A crest on a white background, picked at random from six, with a banner naming the book that waking will open. |
| Custom         | A wallpaper from the SD card. Falls back to Light when none is found.             |
| Cover          | The open book's cover. Falls back to Light when no book is open.                  |
| Cover + Custom | The cover while you are reading, a custom wallpaper otherwise.                    |
| Quick Resume   | The screen you locked from, with a moon icon at the edge.                         |
| Stats Dashboard| Your reading statistics.                                                          |
| Transparent    | A BMP or PNG overlay drawn on top of whatever the screen was showing.             |

On **Light**, waking always opens the book the sleep screen named, even when you locked
from the home menu. The book it names is chosen when the device goes to sleep, so the name
on the sleep screen is always the book you get.

**Quick Resume** is meant to be invisible: the moon appears when you lock, and waking puts
you back exactly where you were with the moon gone. Nothing else is drawn over it, and
"Open Book on Boot" does not apply to it. Because waking is a full restart, only
a book page can be rebuilt, so locking from a menu or a settings screen shows the home
screen from the moment you lock rather than changing under you on the way back.

**Quick Resume on Timeout** turns on the Quick Resume face for sleeps caused by inactivity,
whatever the Sleep Screen setting says.

### Wallpaper files

Wallpapers are `.bmp` or `.pxc` files. The `.pxc` format is the packed 2-bit format the
[Lector converter](https://diogo7dias.github.io/lector-xteink-firmware/) writes.

- **A folder of wallpapers (recommended):** put files in `/sleep`. One is chosen each time
  the device sleeps. A hidden `/.sleep` folder is also read.
- **A single wallpaper:** put `sleep.bmp` or `sleep.pxc` in the root of the card. A root file
  takes priority over the folders.

Size the image to the panel: **480x800** on the X4, **528x792** on the X3.

Wallpaper rows in the in-book menu's Sleep Screen tab favourite, pause, hold and delete the
wallpaper the device just showed. Paused wallpapers move to `/sleep pause` and stop appearing
in the rotation. **Settings > Display > Shuffle Wallpapers** reshuffles the order.

Four Display settings decide what is drawn over a wallpaper: **Show Wallpaper Name**, **Show
Favorite Badge**, **Show Wallpaper Position** and **Sleep Footer Text** (your own line of
text). **Sleep Image Quality** trades rendering time against how the image looks: **Fast** or
**Pretty**.

### Cover settings

For **Cover** and **Cover + Custom**:

- **Sleep Screen Cover Mode** — **Fit** (scaled to fit, white borders) or **Crop** (scaled and
  cropped to fill).
- **Sleep Screen Cover Filter** — **None** (grayscale), **Contrast** (black and white) or
  **Inverted**.

### Transparent overlays

Overlays are drawn over the current screen instead of replacing it, so they need an alpha
channel: a PNG, or a 32-bit BGRA BMP. In a regular BMP, white is treated as transparent.

- **A folder of overlays:** `/.sleep-overlay`, or `/sleep-overlay`.
- **A single overlay:** `/sleep-overlay.bmp` or `/sleep-overlay.png` in the root. A root BMP
  wins over a root PNG, and both win over the folders.

> [!NOTE]
> `.pxc` cannot be an overlay. The format is already quantised to four opaque levels and
> carries no alpha channel.

---

## 9. Getting books onto the device

The simplest route is a card reader: copy files onto the SD card. Everything below is the
wireless alternative.

### File Transfer

**Home > File Transfer** offers four modes:

- **Join a Network** — connect to an existing Wi-Fi network.
- **Create Hotspot** — the device makes a network for your computer or phone to join.
- **Calibre Wireless** — Calibre device transfers, below.
- **Nearby Reader** — receive a file straight from another Lector, below.

In the first two modes the device runs a web server. Open `http://<device-ip>/` in a browser,
or `http://crosspoint.local`. The web interface uploads and downloads files, manages fonts,
and edits Wi-Fi networks and OPDS servers at `http://<device-ip>/settings`. It also speaks
**WebDAV**, so the card can be mounted as a network drive.

The home page shows battery charge, free card space, and the books you have in progress.
The file manager can zip several selected files into one download, and **Fetch from URL**
hands the reader a link so it downloads the file itself instead of you uploading it.

While joined to a network, the screen shows the Wi-Fi signal strength in dBm.

See [docs/webserver.md](./docs/webserver.md) for the full web server documentation, including
managing files from the command line with `curl`.

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

A font family goes over the same way, from **Settings > Reader > Installed Fonts > Send font**.
The faces of the family are sent one after another and the receiving device asks once, naming
the family, how many sizes it holds and the total size. The family installs into `/.fonts/` and
is ready to pick in Text Settings straight away. A family already installed on the receiving
device is refused rather than merged or overwritten; delete it there first to replace it. A
send that stops part way leaves nothing half-installed.

Turn Wi-Fi and File Transfer off first: the radio cannot do both at once, and the device says
so if it is busy.

### OPDS catalogs

**Settings > System > OPDS Servers** stores up to 8 catalogs. Each entry has a name, the
catalog root URL (for a Calibre Content Server, usually ending in `/opds`), and optional
username and password. Authentication is HTTP Basic; set Calibre to Basic rather than Digest.

Browse and download from **Home > OPDS Browser**. Where downloads land (**Download folder**)
and how they are named (**Filename format**: Author - Title, Title - Author, or Title) are set
from the OPDS screens and the web interface, not from the on-device Settings menu.

Catalogs can also be managed from the web interface at `http://<device-ip>/settings`.

---

## 10. Syncing reading position

### Nearby Sync

The fastest way to move a position between two Lector devices. Open the same book on both,
then pick **Nearby Sync** from the in-book menu's Device tab on each. The devices find each
other and show both pages, so you can **Take their page** or **Send my page**.

The screen says what happened: both on the same page, they are further ahead, moved to their
page, and so on. It also says when the other reader has a different book open, when nothing
answered, and when the radio is busy with Wi-Fi or File Transfer.

The position that travels points at the last paragraph on the page you are looking at, so the
other device opens where you were reading rather than a paragraph behind.

### KOReader sync

Lector syncs progress with KOReader-compatible sync servers, so KOReader apps and devices can
share a position with it.

Settings live in **Settings > System > KOReader Sync**: **Username**, **Password**, **Sync
Server URL**, **Document Matching** (Filename or Binary), **Send Document Metadata**, and
**Sync Behavior**.

**Sync Behavior** is **Smart sync** for new configurations, which resolves the simple cases on
its own: upload when the server has nothing, do nothing when both sides agree, upload when
local is ahead, apply the remote when the remote is ahead. Configurations migrated from older
firmware keep **Ask every time**, which offers **Apply Remote** and **Upload Local** on every
sync. Either can be changed at any time.

To sync, open the in-book menu and pick **Sync Progress** from the Device tab, or bind a hold
to **KOSync** in **Settings > Controls**.

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

#### Option B: the public KOReader server

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

#### Option C: your own server

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

Lector ships with Noto Serif and Noto Sans. Fonts on the SD card are added to that list, and
can bring scripts the built-in fonts do not cover, including Chinese, Japanese and Korean.

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
2. **From the web interface:** in File Transfer mode, open the **Fonts** page and upload
   `.cpfont` files.
3. **From your computer:** copy fonts into `/.fonts/` (preferred) or `/fonts/` on the card.
   Files come from the [crosspoint-fonts repository](https://github.com/crosspoint-reader/crosspoint-fonts).

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
**Settings > Controls**. Every successful lookup is remembered: **Lookup History**, in the
same menu, lists the last 100 words newest first and reopens any of them. Setup and usage
are covered in [docs/dictionary.md](./docs/dictionary.md).

---

## 12. Settings reference

Settings are split into four categories: **Display**, **Reader**, **Controls** and **System**.

### 12.1 Display

**Sleep screen**

- **Sleep Screen** — Light, Custom, Cover, Cover + Custom, Quick Resume, Stats
  Dashboard, Transparent. See [section 8](#8-sleep-screen-and-wallpapers).
- **Quick Resume on Timeout** — ON or OFF.
- **Wake Straight to Book** — wake goes straight back into the book.
- **Sleep Footer Text** — your own line of text on the sleep screen.

**Wallpaper**

- **Sleep Screen Cover Mode** — Fit or Crop.
- **Sleep Screen Cover Filter** — None, Contrast or Inverted.
- **Sleep Image Quality** — Fast or Pretty.
- **Show Wallpaper Name**, **Show Favorite Badge**, **Show Wallpaper Position** — what is
  drawn over the wallpaper.
- **Shuffle Wallpapers** — reshuffle the rotation now.

**Screen**

- **Refresh Frequency** — a full refresh every 1, 5, 10, 15 or 30 pages.
- **Sunlight Fading Fix** — software fix for white X4 units fading in direct sunlight.

**Home**

- **Author On Home** — Initials or Full Name.

### 12.2 Reader

**Text** — **Text Settings** (below), **Manage Fonts**, **Installed Fonts**, **Dictionary**.

**Text Settings** is one scrolling list, banded into **Type**, **Spacing**, **Margins** and
**Reading Aids**, under a live preview. The preview is a split slice of a real page: the top
of the page, a dashed cut, then the bottom of the page, so both vertical margins and the
status bar are on screen at their true size while you tune them. Numeric rows are edited in
place — Select arms the row, Up and Down move the value, and the preview follows — so nothing
ever covers the page you are judging.

- **Font** — Noto Serif, Noto Sans, plus any SD card families; opens a full-screen picker.
- **Size**.
- **Line Spacing**.
- **Horizontal Margin** — left and right, one shared value.
- **Link Top/Bottom** — ON gives a single **Vertical Margin**; OFF splits it into **Top
  Margin** and **Bottom Margin**. (This replaces the old **Uniform Margins** switch. Devices
  updating from an older build keep the margins they had.)
- **Dynamic Margins** — Off, Auto (min 10px) or Auto (min 20px).
- **First Line Indent** — Book or Custom %, with **First-Line Indent %** underneath.
- **Paragraph Alignment** — Justify, Left, Center, Right or Book's Style.
- **Embedded Text Style** — honour the book's own CSS for how the words look: bold, italic,
  underline, superscript and subscript, writing direction, and passages the book marks as
  hidden. Bold and italic written as `<b>` or `<i>` tags are always honoured, switch or not.
- **Embedded Layout Style** — honour the book's own CSS for where blocks sit: alignment,
  first-line indent, margins, padding and image sizes. **Paragraph Alignment: Book's Style**
  and **First Line Indent: Book** both read the book's CSS, so they need this on.
- **Bionic Reading** — bolds the first part of each word as a fixation point.
- **Guide Dots** — draws a middle dot in a widened gap between words.
- **Hidden Dots** — only listed while Guide Dots is on: keeps the widened gaps and draws no dot in them.
- **Hyphenation**.
- **Extra Paragraph Spacing** — space between paragraphs instead of a first-line indent.
- **Text Anti-Aliasing** — smoother edges, slightly slower page turns.
- **Debug Layout Borders** — draws the layout boxes; a diagnostic, not a reading feature.

**Page**

- **Reading Orientation** — Portrait, Landscape CW, Portrait 180°, Landscape CCW.
- **Paragraph Numbers** — Off or Per Chapter, with **Number Size** (Small or Double).
- **Book Menu Opens On** — which tab the in-book menu starts on: Navigate, Book, Look or Device.

**Look**

- **Paperback Look**, **Paperback Status Bar**.
- **Night mode** — inverts the screen.
- **Customise Status Bar** — see below.

**Customise Status Bar** places each element in one of six slots (top left, top centre, top
right, bottom left, bottom centre, bottom right) or turns it off: **Battery**, **Clock**,
**Title**, **Page in Chapter**, **Book %**, **Chapter %**, **Chapter Number**, **Pages This
Session**. Alongside them:

- **Title Source** — Book or Chapter, and **Truncate Title**.
- **Page Format** — `N/M` or `N left`.
- **Book Bar** and **Chapter Bar** — Off, Top or Bottom, with **Bar Thickness** (Slim, Medium,
  Fat), **Floating Bar** and **Bar Outline**.
- **Progress Bar** — Off, Slim, Medium or Fat, for when the status bar itself is off.
- **XTC Status Bar** — Hide, Bottom or Top, for XTC books.
- **Clock UTC Offset**, **Clock Format** (24-hour or 12-hour) and **Clock Synced**.

### 12.3 Controls

**Buttons**

- **Remap Front Buttons** — reassign each bottom-edge button.
- **Orient front buttons** — rotate their meaning with the screen.
- **Side Button Layout (reader)** — Prev/Next, Next/Prev or Disabled.

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

- **Short Back to File Browser** — swap the short and long press of Back in a book.
- **Home Back Button** — None, Resume or Reading Stats.

### 12.4 System

**Power** — **Time to Sleep**: 1 to 30 minutes, or Never. Default 10 minutes.

**Library**

- **Show Hidden Files**.
- **File Browser Order** — Alphabetical, Random, Recently Added or Last Read.
- **Open Book on Boot** — Off, Last Book or Random Book.
- **Clear Read Books from Recent List**.
- **Move Finished Books to Read Folder**.
- **Move Opened Books to Recents Folder**.

**Stats** — **Track Reading Stats** and **Reading Idle Limit**.

**Fast Page Turns** — on by default on the X4, where it is validated. Page turns and menu
moves use the panel's cheapest waveform, which is several times quicker than the standard
one. Every eighth pass runs the standard waveform anyway, so the panel never works from a
stale temperature reading, and the first paint after a wake never uses it. Turn it off if
you ever see ink left behind that a full refresh does not clear.

**Performance Timings** — off by default, and free while it is off. On, Lector times every
screen refresh and reports the numbers three ways: a small line in the top-left corner of
the screen showing what the previous refresh cost, a breakdown of the last wake in place of
the unlock screen footer, and a `.csv` file per session in the `/perf` folder on the card.
It is there so a slow page turn can be measured rather than guessed at. Turn it off again
when you are done; it writes to the card while it is on.

**Network** — **Wi-Fi Networks**, **KOReader Sync**, **OPDS Servers**.

**Device**

- **Language** — 31 languages: English, Spanish, French, German, Czech, Portuguese (Portugal
  and Brazil), Russian, Swedish, Romanian, Catalan, Ukrainian, Belarusian, Italian, Polish,
  Finnish, Danish, Dutch, Turkish, Kazakh, Hungarian, Lithuanian, Slovenian, Valencian,
  Hebrew, Slovak, Vietnamese, Norwegian, Arabic, Bosnian and Indonesian.
- **Device Name** — how this reader introduces itself to another during Nearby Sync. Left
  empty, it falls back to a generated `Lector-XXXX` name derived from the MAC address.
- **Clean Up Storage** — removes cached data for books that are no longer on the card. Books
  keep their progress.
- **Clear Reading Cache**.
- **Check for updates** and **SD Card Firmware Update** — see
  [section 13](#13-updating-the-firmware).

---

## 13. Updating the firmware

Three routes:

- **Over Wi-Fi:** **Settings > System > Check for updates**. Only stable releases are
  offered, never experimental ones. A device already running an experimental build is
  offered the next stable release, which is how it gets back onto a stable line without a
  cable.
- **From the SD card:** put a firmware `.bin` on the card and use **Settings > System > SD
  Card Firmware Update**, or open the `.bin` from the file browser.
- **Over USB:** the [Lector flasher](https://diogo7dias.github.io/lector-xteink-firmware/)
  installs over Web Serial from a browser.

If the device will not boot far enough to reach Settings, hold **Volume Up** together with
**Power** at boot. That goes straight to the SD card firmware update screen, which is the way
back on devices where USB flashing is locked down.

---

## 14. Troubleshooting

**The device is stuck in a bootloop.** Press and release **Reset**, then hold **Back** and
**Power** to boot to the Home screen. If that does not work, hold **Volume Up** and **Power**
at boot to reach the SD card firmware update screen.

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
- **GIFs and progressive JPEGs** in EPUBs are not rendered, and fall back to an `[Image]`
  placeholder. Ordinary JPEG and PNG images render.

Bugs and logs are welcome as issue tickets.
