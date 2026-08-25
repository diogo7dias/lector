#!/usr/bin/env python3
"""Build the CrossPoint-vs-Lector side-by-side screen mockups for the Xteink X4 Pro.

Geometry comes from the firmware itself: BaseMetrics::values in
src/components/themes/BaseTheme.h (both trees), BaseTheme::drawButtonHints for the
hint boxes, and the generated font headers for the UI font sizes. The panel is the
X4 Pro's 800x480 glass driven portrait, so every frame is 480x800 device pixels.
"""

import base64
import html
import json
import os

W, H = 480, 800
OUT = "/tmp/mock/x4pro-crosspoint-vs-lector.html"
FONT_DIR = "/tmp/mock/fonts"
STRINGS = json.load(open("/tmp/mock/strings.json"))


def S(key, fallback=None):
    return STRINGS.get(key, fallback if fallback is not None else key)


# --- per-tree constants -----------------------------------------------------
# Font sizes are the converted ppem: fontconvert.py runs at 150 dpi, so
# ppem = size * 150 / 72. Line heights are the yAdvance field of the generated header.
CP = dict(  # upstream CrossPoint (develop)
    name="CrossPoint",
    ui=20.83, ui_lh=24, small=16.67, small_lh=20, big=25.0, big_lh=29,
    font="Ubuntu", small_font="NotoSans",
    top_padding=5,
)
LE = dict(  # our fork
    name="Lector",
    ui=25.0, ui_lh=25, small=20.83, small_lh=21, big=29.17, big_lh=29,
    font="Cozette", small_font="Cozette",
    top_padding=14,
)

HEADER_H = 45
VSPACE = 10
SIDE_PAD = 20
ROW_H = 30
ROW_SUB_H = 50
MENU_ROW_H = 45
MENU_SPACING = 8
HINTS_H = 40
HINT_W = 106
HINT_X = [25, 130, 245, 350]
HOME_TOP_PADDING = 40
HOME_TILE_H = 400
HOME_MENU_OFFSET = 10
BATTERY_W, BATTERY_H = 15, 12


# --- primitives -------------------------------------------------------------
parts = []


def esc(t):
    return html.escape(str(t))


def div(cls, x, y, w=None, h=None, style="", text=""):
    s = f"left:{x}px;top:{y}px;"
    if w is not None:
        s += f"width:{w}px;"
    if h is not None:
        s += f"height:{h}px;"
    return f'<div class="{cls}" style="{s}{style}">{text}</div>'


def text(t, x, y, size, lh, font, weight="regular", color="#000", align="left", w=None):
    fam = f"{font}{'B' if weight == 'bold' else ''}"
    style = (f"font-family:{fam};font-size:{size}px;line-height:{lh}px;"
             f"color:{color};text-align:{align};")
    return div("t", x, y, w, lh, style, esc(t))


class Frame:
    """One 480x800 device screen."""

    def __init__(self, theme, caption):
        self.th = theme
        self.caption = caption
        self.els = []

    # -- text helpers, sized from the theme's own UI fonts
    def ui(self, t, x, y, **kw):
        return self.add(text(t, x, y, self.th["ui"], self.th["ui_lh"], self.th["font"], **kw))

    def small(self, t, x, y, **kw):
        return self.add(text(t, x, y, self.th["small"], self.th["small_lh"], self.th["small_font"], **kw))

    def big(self, t, x, y, **kw):
        return self.add(text(t, x, y, self.th["big"], self.th["big_lh"], self.th["font"], **kw))

    def add(self, el):
        self.els.append(el)
        return self

    def rect(self, x, y, w, h, fill=False, stroke=True, extra=""):
        style = f"background:{'#000' if fill else 'transparent'};"
        if stroke:
            style += "border:1px solid #000;"
        return self.add(div("r", x, y, w, h, style + extra))

    def line(self, x, y, w, thick=1):
        return self.add(div("r", x, y, w, thick, "background:#000;"))

    # -- firmware chrome
    def header(self, title=None, subtitle=None, battery="87%"):
        top = self.th["top_padding"]
        bx = W - 10 - BATTERY_W
        self.rect(bx, top + 5, BATTERY_W, BATTERY_H)
        self.rect(bx + 3, top + 8, BATTERY_W - 7, BATTERY_H - 6, fill=True, stroke=False)
        self.rect(bx + BATTERY_W, top + 8, 2, 6, fill=True, stroke=False)
        self.small(battery, bx - 46, top + 6, w=42, align="right")
        if title:
            self.ui(title, 0, top + 5, w=W, align="center")
        if subtitle:
            self.small(subtitle, 0, H - HINTS_H - self.th["small_lh"] - 6, w=W - SIDE_PAD, align="right")
        return self

    def hints(self, labels):
        y = H - HINTS_H
        for i, lab in enumerate(labels):
            if not lab:
                continue
            self.rect(HINT_X[i], y, HINT_W, HINTS_H, fill=False)
            self.ui(lab, HINT_X[i], y + 7, w=HINT_W, align="center")
        return self

    def list_rows(self, rows, y, selected=0, row_h=ROW_H, values=None, headings=None):
        """rows: list of str. values: optional right-aligned values. headings: set of indexes
        that are section headings (fork-only concept, drawn small + rule)."""
        cur = y
        for i, row in enumerate(rows):
            if headings and i in headings:
                self.small(row, SIDE_PAD, cur + 4, weight="bold")
                self.line(SIDE_PAD, cur + self.th["small_lh"] + 6, W - SIDE_PAD * 2)
                cur += self.th["small_lh"] + 12
                continue
            sel = (i == selected)
            if sel:
                self.rect(SIDE_PAD - 6, cur, W - (SIDE_PAD - 6) * 2, row_h, fill=True, stroke=False)
            colour = "#fff" if sel else "#000"
            self.ui(row, SIDE_PAD, cur + (row_h - self.th["ui_lh"]) // 2, color=colour)
            if values and values[i]:
                self.ui(values[i], SIDE_PAD, cur + (row_h - self.th["ui_lh"]) // 2,
                        color=colour, align="right", w=W - SIDE_PAD * 2)
            cur += row_h
        return cur

    def menu(self, items, y, selected=-1):
        cur = y
        for i, it in enumerate(items):
            sel = (i == selected)
            self.rect(SIDE_PAD, cur, W - SIDE_PAD * 2, MENU_ROW_H, fill=sel)
            self.ui(it, SIDE_PAD + 44, cur + (MENU_ROW_H - self.th["ui_lh"]) // 2,
                    color="#fff" if sel else "#000")
            # icon box, stands in for the bitmap icons in src/components/icons
            self.rect(SIDE_PAD + 12, cur + (MENU_ROW_H - 20) // 2, 20, 20,
                      fill=False, extra="border-color:%s;" % ("#fff" if sel else "#000"))
            cur += MENU_ROW_H + MENU_SPACING
        return cur

    def scrollbar(self, y, h, frac_top=0.0, frac_len=0.4):
        self.add(div("r", W - 9, y, 4, h, "background:#ddd;"))
        self.add(div("r", W - 9, int(y + h * frac_top), 4, int(h * frac_len), "background:#000;"))
        return self

    def render(self):
        return (f'<div class="frame"><div class="screen">{"".join(self.els)}</div>'
                f'<div class="cap">{esc(self.caption)}</div></div>')


SECTIONS = []


def section(title, note, cp_frame=None, le_frame=None, only=None):
    SECTIONS.append(dict(title=title, note=note, cp=cp_frame, le=le_frame, only=only))


# =========================================================================
# 1. Home
# =========================================================================
def home_cp():
    f = Frame(CP, "CrossPoint: cover tile + Recent Books row")
    f.header()
    # drawRecentBookCover: one big cover tile
    f.rect(60, HOME_TOP_PADDING + 10, 360, HOME_TILE_H - 30)
    f.small("cover.bmp", 60, HOME_TOP_PADDING + HOME_TILE_H // 2, w=360, align="center", color="#888")
    y = HOME_TOP_PADDING + HOME_TILE_H + HOME_MENU_OFFSET
    f.menu([S("STR_BROWSE_FILES"), S("STR_MENU_RECENT_BOOKS", "Recent Books"),
            S("STR_FILE_TRANSFER"), S("STR_SETTINGS_TITLE")], y, selected=0)
    f.hints([S("STR_RESUME"), S("STR_SELECT"), S("STR_DIR_UP"), S("STR_DIR_DOWN")])
    return f


def home_le():
    f = Frame(LE, "Lector: in-progress list, [NN%] badges, version + clock")
    top = LE["top_padding"]
    f.header()
    f.small("0.27.0-x4pro", SIDE_PAD, top + 5)
    f.small("14:32", W - 10 - BATTERY_W - 46 - 60, top + 6, w=56, align="right")
    y = HOME_TOP_PADDING + 22
    books = [("The Left Hand of Darkness", "Ursula K. Le Guin", "42%"),
             ("Blood Meridian", "Cormac McCarthy", "7%"),
             ("Piranesi", "Susanna Clarke", "88%")]
    for i, (t, a, pct) in enumerate(books):
        sel = (i == 0)
        rh = 62
        if sel:
            f.rect(SIDE_PAD - 6, y, W - (SIDE_PAD - 6) * 2, rh, fill=True, stroke=False)
        col = "#fff" if sel else "#000"
        f.ui(t, SIDE_PAD, y + 4, color=col, w=W - SIDE_PAD * 2 - 60)
        f.rect(W - SIDE_PAD - 56, y + 6, 56, LE["small_lh"] + 4, fill=not sel, stroke=sel)
        f.small("[%s]" % pct, W - SIDE_PAD - 56, y + 8, w=56, align="center",
                color="#000" if sel else "#fff")
        f.small(a, SIDE_PAD, y + 4 + LE["ui_lh"] + 2, color=col)
        y += rh + 4
    menu_items = [S("STR_BROWSE_FILES"), S("STR_FILE_TRANSFER"), S("STR_SETTINGS_TITLE")]
    block = VSPACE + (len(menu_items) - 1) * (MENU_ROW_H + MENU_SPACING) + MENU_ROW_H
    menu_top = max(HOME_TOP_PADDING + HOME_TILE_H + HOME_MENU_OFFSET,
                   H - HINTS_H - MENU_SPACING - block)
    f.menu(menu_items, menu_top, selected=-1)
    f.hints([S("STR_STATS"), S("STR_SELECT"), S("STR_DIR_UP"), S("STR_DIR_DOWN")])
    return f


section("Home", "Upstream shows one cover tile and a Recent Books menu row. Lector drops cover "
                "generation for a wrapped in-progress list with inline percent badges, and puts the "
                "firmware version and the clock in the header band.", home_cp(), home_le())


# =========================================================================
# 2. File browser
# =========================================================================
FILES = [("Le Guin - The Left Hand of Darkness.epub", "42%"),
         ("McCarthy - Blood Meridian.epub", "7%"),
         ("Clarke - Piranesi.epub", "88%"),
         ("notes.txt", ""),
         ("Calibre Library/", ""),
         ("Wallpapers/", "")]


def browser(theme, caption, wrapped):
    f = Frame(theme, caption)
    top = theme["top_padding"]
    f.header(S("STR_SD_CARD"))
    y = top + HEADER_H + VSPACE
    if wrapped:
        cur = y
        for i, (name, pct) in enumerate(FILES):
            sel = (i == 0)
            if len(name) < 34:
                lines = [name]
            else:
                cut = name.rfind(" ", 0, 34)
                cut = cut if cut > 12 else 33
                lines = [name[:cut], name[cut:].lstrip()]
            rh = 6 + len(lines) * theme["ui_lh"]
            if sel:
                f.rect(SIDE_PAD - 6, cur, W - (SIDE_PAD - 6) * 2, rh, fill=True, stroke=False)
            col = "#fff" if sel else "#000"
            for li, ln in enumerate(lines):
                f.ui(ln, SIDE_PAD + 26, cur + 3 + li * theme["ui_lh"], color=col,
                     w=W - SIDE_PAD * 2 - 26 - (58 if pct else 0))
            f.rect(SIDE_PAD, cur + 6, 16, 16, fill=False,
                   extra="border-color:%s;" % col)
            if pct:
                f.rect(W - SIDE_PAD - 52, cur + 5, 52, theme["small_lh"] + 4,
                       fill=not sel, stroke=sel)
                f.small("[%s]" % pct, W - SIDE_PAD - 52, cur + 7, w=52, align="center",
                        color="#000" if sel else "#fff")
            cur += rh
    else:
        for i, (name, _pct) in enumerate(FILES):
            sel = (i == 0)
            cur = y + i * ROW_H
            if sel:
                f.rect(SIDE_PAD - 6, cur, W - (SIDE_PAD - 6) * 2, ROW_H, fill=True, stroke=False)
            col = "#fff" if sel else "#000"
            shown = name if len(name) < 30 else name[:28] + "…"
            f.ui(shown, SIDE_PAD + 26, cur + (ROW_H - theme["ui_lh"]) // 2, color=col)
            f.rect(SIDE_PAD, cur + 6, 16, 16, extra="border-color:%s;" % col)
    # full path strip above the hints (both trees draw it)
    path_y = H - HINTS_H - VSPACE - theme["small_lh"]
    f.line(0, path_y - VSPACE // 2, W, 3)
    f.small("/", SIDE_PAD, path_y)
    f.hints([S("STR_BACK"), S("STR_SELECT"), S("STR_DIR_UP"), S("STR_DIR_DOWN")])
    return f


section("File browser",
        "Same chrome both sides. Upstream truncates a long title with an ellipsis; Lector wraps it "
        "over as many lines as it needs and carries the read-percent badge on the row.",
        browser(CP, "CrossPoint: fixed 30 px rows, ellipsis", False),
        browser(LE, "Lector: wrapped rows, percent badge", True))


# =========================================================================
# 3. Reader page
# =========================================================================
BODY = ("The king was in his counting-house, and the frost had come down in the night. "
        "She walked the long corridor twice before she understood that the doors were not "
        "doors at all, and that the hall had no end she could reach by walking. "
        "Outside, the snow went on falling into the sea, which took it without comment. "
        "There would be no answer before morning, and perhaps none after.")


def reader(theme, caption, v2):
    f = Frame(theme, caption)
    body = BODY + " " + BODY
    f.add(div("t", 28, 46, W - 56, 640,
              "font-family:Serif;font-size:31px;line-height:38px;color:#000;"
              "white-space:normal;text-align:justify;", esc(body)))
    if v2:
        # Lector status bar v2: per-item clusters on both bands, hard against the edge
        f.small("The Left Hand of Darkness", SIDE_PAD, 6)
        f.small("14:32", 0, 6, w=W - SIDE_PAD, align="right")
        f.small("Ch 4 · The Nineteenth Day", SIDE_PAD, H - 10 - theme["small_lh"])
        f.small("42%  ·  312/740", 0, H - 10 - theme["small_lh"], w=W - SIDE_PAD, align="right")
        f.add(div("r", 0, H - 4, int(W * 0.42), 4, "background:#000;"))
    else:
        f.small("The Left Hand of Darkness", SIDE_PAD, 12)
        f.small("42%", 0, H - 54, w=W - SIDE_PAD, align="right")
        f.rect(SIDE_PAD, H - 30, W - SIDE_PAD * 2, 16)
        f.rect(SIDE_PAD, H - 30, int((W - SIDE_PAD * 2) * 0.42), 16, fill=True)
    return f


section("Reader page",
        "Upstream draws a single bar plus a boxed progress bar. Lector's v2 status bar places "
        "independent items in six anchors across a top and a bottom band, reflows them when they "
        "collide, and runs the progress rule to the panel edge.",
        reader(CP, "CrossPoint: title bar + boxed progress bar", False),
        reader(LE, "Lector: v2 status bar, edge-to-edge rule", True))


# =========================================================================
# 4. Reader menu
# =========================================================================
def reader_menu(theme, caption, groups):
    f = Frame(theme, caption)
    top = theme["top_padding"]
    f.header("The Left Hand of Darkness")
    y = top + HEADER_H + VSPACE
    if groups:
        rows = [S("STR_GRP_PAGE", "PAGE"), S("STR_GO_TO_PERCENT"), S("STR_GO_TO_PARAGRAPH"),
                S("STR_SELECT_CHAPTER"),
                S("STR_GRP_MARKS", "MARKS"), S("STR_BOOKMARKS"), S("STR_GRAB_QUOTE"),
                S("STR_FOOTNOTES"),
                S("STR_GRP_BOOK", "BOOK"), S("STR_READING_STATS", "Reading Stats"),
                S("STR_TEXT_SETTINGS"), S("STR_CUSTOMISE_STATUS_BAR")]
        heads = {0, 4, 8}
    else:
        rows = [S("STR_GO_TO_PERCENT"), S("STR_GO_TO_PARAGRAPH"), S("STR_SELECT_CHAPTER"),
                S("STR_BOOKMARKS"), S("STR_FOOTNOTES"), S("STR_TEXT_SETTINGS"),
                S("STR_DISPLAY_QR"), S("STR_DELETE_CACHE")]
        heads = None
    f.list_rows(rows, y, selected=1 if groups else 0, headings=heads)
    f.scrollbar(y, H - y - HINTS_H - VSPACE, 0, 0.55)
    f.hints([S("STR_BACK"), S("STR_SELECT"), S("STR_DIR_UP"), S("STR_DIR_DOWN")])
    return f


section("Reader menu",
        "Lector groups the in-book actions under section headings and adds Grab Quote, Reading "
        "Stats and Customise Status Bar.",
        reader_menu(CP, "CrossPoint: flat action list", False),
        reader_menu(LE, "Lector: grouped, extra actions", True))


# =========================================================================
# 5. Text settings
# =========================================================================
def text_settings(theme, caption, extra):
    f = Frame(theme, caption)
    top = theme["top_padding"]
    f.header(S("STR_TEXT_SETTINGS"))
    y = top + HEADER_H + VSPACE
    rows = [S("STR_FONT", "Font"), S("STR_FONT_SIZE", "Font Size"), S("STR_LINE_SPACING", "Line Spacing"),
            S("STR_MARGINS", "Margins"), S("STR_ALIGNMENT"), S("STR_DYNAMIC_MARGINS"),
            S("STR_EMBEDDED_TEXT_STYLE"), S("STR_EMBEDDED_LAYOUT_STYLE")]
    vals = ["Vollkorn", "16", "1.4", "24", S("STR_ALIGN_LEFT"), S("STR_DYNAMIC_MARGINS_10"),
            S("STR_STATE_ON", "On"), S("STR_STATE_OFF", "Off")]
    if extra:
        rows += [S("STR_READING_THEMES"), S("STR_STEAL_LOOK"), S("STR_EXTRA_SPACING")]
        vals += ["3 saved", "—", S("STR_STATE_OFF", "Off")]
    f.list_rows(rows, y, selected=1, values=vals)
    f.hints([S("STR_BACK"), S("STR_ADJUST"), S("STR_DIR_UP"), S("STR_DIR_DOWN")])
    return f


section("Text settings",
        "Lector adds saved reading presets and Steal Look (copy another book's look) to the same list.",
        text_settings(CP, "CrossPoint", False),
        text_settings(LE, "Lector: presets + Steal Look", True))


# =========================================================================
# 6. Settings list
# =========================================================================
def settings_screen(theme, caption, grouped):
    f = Frame(theme, caption)
    top = theme["top_padding"]
    f.header(S("STR_SETTINGS_TITLE"), subtitle="0.27.0-x4pro" if grouped else "1.5.0")
    y = top + HEADER_H + VSPACE
    if grouped:
        rows = [S("STR_GRP_SLEEP_SCREEN", "SLEEP SCREEN"), S("STR_SLEEP_SCREEN"),
                S("STR_QUICK_RESUME_TIMEOUT"), S("STR_WAKE_STRAIGHT_TO_BOOK"),
                S("STR_GRP_WALLPAPER", "WALLPAPER"), S("STR_SLEEP_COVER_MODE"),
                S("STR_SHUFFLE_WALLPAPERS"),
                S("STR_GRP_FRONTLIGHT", "LIGHT"), S("STR_FRONTLIGHT"),
                S("STR_FRONTLIGHT_BRIGHTNESS"), S("STR_FRONTLIGHT_WARMTH"),
                S("STR_FRONTLIGHT_RESTORE_ON_WAKE")]
        vals = ["", "Cover", "5 min", S("STR_STATE_ON", "On"),
                "", "Fit", "",
                "", S("STR_STATE_ON", "On"), "18", "40", S("STR_STATE_OFF", "Off")]
        heads = {0, 4, 7}
        sel = 8
    else:
        rows = [S("STR_SLEEP_SCREEN"), S("STR_QUICK_RESUME_TIMEOUT"), S("STR_WAKE_STRAIGHT_TO_BOOK"),
                S("STR_SLEEP_COVER_MODE"), S("STR_REFRESH_FREQ"), S("STR_SUNLIGHT_FADING_FIX"),
                S("STR_AUTHOR_DISPLAY"), S("STR_ORIENTATION"), S("STR_PARAGRAPH_NUMBERS")]
        vals = ["Cover", "5 min", S("STR_STATE_ON", "On"), "Fit", "10", S("STR_STATE_OFF", "Off"),
                "Author", "Portrait", S("STR_STATE_OFF", "Off")]
        heads = None
        sel = 0
    f.list_rows(rows, y, selected=sel, values=vals, headings=heads)
    f.scrollbar(y, H - y - HINTS_H - VSPACE, 0, 0.35)
    f.hints([S("STR_BACK"), S("STR_SELECT"), S("STR_DIR_UP"), S("STR_DIR_DOWN")])
    return f


section("Settings",
        "One long list on both. Lector splits it into named sections and hides the whole LIGHT "
        "section on a board with no frontlight, which is what makes the X4 Pro rows appear.",
        settings_screen(CP, "CrossPoint: flat list", False),
        settings_screen(LE, "Lector: sectioned, frontlight rows", True))


# =========================================================================
# 7. WiFi
# =========================================================================
def wifi(theme, caption, credentials_row):
    f = Frame(theme, caption)
    top = theme["top_padding"]
    f.header(S("STR_WIFI_NETWORKS"))
    y = top + HEADER_H + VSPACE
    rows = ["Vodafone-7A21", "MEO-WiFi", "eduroam", "Casa do Diogo", S("STR_ADD_HIDDEN_NETWORK")]
    vals = [S("STR_CONNECTED"), "", "", "", ""]
    if credentials_row:
        rows.append(S("STR_SHARE_CREDENTIALS"))
        vals.append("")
    f.list_rows(rows, y, selected=0, values=vals)
    f.hints([S("STR_BACK"), S("STR_CONNECT"), S("STR_DIR_UP"), S("STR_DIR_DOWN")])
    return f


section("WiFi networks",
        "Lector adds sharing the saved credentials with another reader over the Nearby radio.",
        wifi(CP, "CrossPoint", False), wifi(LE, "Lector: + Share Credentials", True))


# =========================================================================
# 8. Keyboard
# =========================================================================
KEYS = ["1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"]


def keyboard(theme, caption, big):
    """big=False mirrors today's firmware (keyboardKeyHeight 48, top-anchored under the
    field). big=True is the proposed touch layout: taller keys, bottom-anchored."""
    f = Frame(theme, caption)
    top = theme["top_padding"]
    f.header(S("STR_ENTER_WIFI_PASSWORD"))
    y = top + HEADER_H + VSPACE + 10
    fw = int(W * 0.85)
    fx = (W - fw) // 2
    f.rect(fx, y, fw, 44)
    f.ui("hunter2\u2022", fx + 6, y + (44 - theme["ui_lh"]) // 2)
    kh = 62 if big else 48
    kw = int(W * 0.94)
    kx = (W - kw) // 2
    gap = 3 if big else 0
    if big:
        # bottom-anchored: last key row sits one gap above the hint band
        ky = H - HINTS_H - 12 - (4 * kh + 3 * gap)
    else:
        ky = y + 80
    for r, row in enumerate(KEYS):
        cell = kw // 10
        offs = kx + (kw - cell * len(row)) // 2
        for c, ch in enumerate(row):
            sel = (r == 1 and c == 0)
            x = offs + c * cell
            yy = ky + r * (kh + gap)
            f.rect(x, yy, cell - gap, kh, fill=sel)
            f.ui(ch, x, yy + (kh - theme["ui_lh"]) // 2,
                 w=cell - gap, align="center", color="#fff" if sel else "#000")
    hint_y = ky - theme["small_lh"] - 10
    f.small(S("STR_KB_TIPS", "Hold a key for its second character"), 0, hint_y, w=W, align="center")
    if big:
        f.small("key 45 \u00d7 62 px  \u00b7  target \u2265 48 px", SIDE_PAD, y + 60)
    f.hints([S("STR_BACK"), S("STR_SELECT"), S("STR_DIR_LEFT"), S("STR_DIR_RIGHT")])
    return f


section("Keyboard (revised)",
        "Left is what both trees draw today: keyboardKeyHeight 48, 94% width, top-anchored under "
        "the text field. Right is the proposed touch layout: 62 px keys with a 3 px gap, the block "
        "anchored to the bottom of the screen so a thumb reaches it, and the tips line moved above "
        "the keys. Every key clears the 48 px touch-target floor.",
        keyboard(LE, "Lector today: 45 \u00d7 48 px keys, top-anchored", False),
        keyboard(LE, "Proposed: 45 \u00d7 62 px keys, bottom-anchored", True))


# =========================================================================
# 9. Chapter selection
# =========================================================================
def chapters(theme, caption, with_pct):
    f = Frame(theme, caption)
    top = theme["top_padding"]
    f.header(S("STR_SELECT_CHAPTER"))
    y = top + HEADER_H + VSPACE
    rows = ["Prologue", "1. A Parade in Erhenrang", "2. The Place Inside the Blizzard",
            "3. The Mad King", "4. The Nineteenth Day", "5. The Domestication of Hunch"]
    vals = ["1%", "4%", "11%", "19%", "26%", "33%"] if with_pct else [""] * len(rows)
    f.list_rows(rows, y, selected=4, values=vals)
    f.scrollbar(y, H - y - HINTS_H - VSPACE, 0.1, 0.5)
    f.hints([S("STR_BACK"), S("STR_SELECT"), S("STR_DIR_UP"), S("STR_DIR_DOWN")])
    return f


section("Chapter selection", "Lector carries each chapter's start percent on the row.",
        chapters(CP, "CrossPoint", False), chapters(LE, "Lector: + start percent", True))


# =========================================================================
# 10. Sleep screen
# =========================================================================
def sleep_real():
    """What stats_dashboard::render draws today: cover left (296x444 at x=20, y=70 from
    reading_stats::dashboardLayout), a right-aligned column of 7 value/label rows, footer."""
    f = Frame(LE, "Lector today: cover left, 7 stat rows right")
    cw, ch = 296, 444
    f.rect(20, 70, cw, ch, extra="border-radius:8px;")
    f.small("cover", 20, 70 + ch // 2, w=cw, align="center", color="#888")
    rx = W - 20
    rows = [("42%", S("STR_STATS_COMPLETED")), ("4 h 12 m", S("STR_STATS_TIME_READ", "Time read")),
            ("~5 h", S("STR_STATS_EST_FINISH")), ("22 min", S("STR_STATS_AVG_SESSION")),
            ("0.8", "pages/min"), ("312", "pages"), ("9", "days")]
    ry = 70
    for val, lab in rows:
        f.big(val, rx - 120, ry, w=120, align="right")
        f.small(lab, rx - 120, ry + LE["big_lh"] + 1, w=120, align="right")
        ry += LE["big_lh"] + LE["small_lh"] + 14
    f.ui("The Left Hand of Darkness", SIDE_PAD, H - 97, w=W - SIDE_PAD * 2)
    f.small("Ursula K. Le Guin  ·  Ch 4", SIDE_PAD, H - 97 + LE["ui_lh"] + 4)
    return f


def sleep_proposed():
    f = Frame(LE, "Proposed: title block, progress rule, 4 stat cards, weekday bars")
    f.big(S("STR_STATS_CURRENT_BOOK", "Current book"), 0, 44, w=W, align="center")
    f.ui("The Left Hand of Darkness", 0, 84, w=W, align="center")
    f.small("Ursula K. Le Guin  ·  Ch 4, The Nineteenth Day", 0, 116, w=W, align="center")
    bar_x, bar_w = SIDE_PAD + 30, W - (SIDE_PAD + 30) * 2
    f.rect(bar_x, 156, bar_w, 14)
    f.rect(bar_x, 156, int(bar_w * 0.42), 14, fill=True)
    f.small("42%  ·  4 h 12 m read  ·  ~5 h left", 0, 178, w=W, align="center")
    boxes = [(S("STR_STATS_TODAY", "Today"), "38 min"), (S("STR_STATS_STREAK", "Streak"), "9 days"),
             (S("STR_STATS_AVG_SESSION"), "22 min"), ("Pages", "312")]
    bw = (W - SIDE_PAD * 2 - 12) // 2
    for i, (lab, val) in enumerate(boxes):
        bx = SIDE_PAD + (i % 2) * (bw + 12)
        by = 226 + (i // 2) * 108
        f.rect(bx, by, bw, 94)
        f.small(lab, bx, by + 12, w=bw, align="center")
        f.big(val, bx, by + 42, w=bw, align="center")
    days = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
    heights = [30, 48, 22, 66, 40, 84, 58]
    bwd = (W - SIDE_PAD * 2) // 7
    base = 560
    for i, (d, hgt) in enumerate(zip(days, heights)):
        bx = SIDE_PAD + i * bwd
        f.rect(bx + 4, base + 100 - hgt, bwd - 12, hgt, fill=True, stroke=False)
        f.small(d, bx, base + 106, w=bwd - 8, align="center")
    f.small("Evening reader  ·  best day Saturday", 0, base + 140, w=W, align="center")
    f.small("Sleeping  ·  press power to wake", 0, H - 44, w=W, align="center")
    return f


section("Sleep screen (redesign)",
        "Left is what stats_dashboard::render draws today: the cover on the left and a right-aligned "
        "column of seven value-over-label rows (geometry from reading_stats::dashboardLayout — cover "
        "296x444 at x=20, y=70, footer 97 px up). Right is the layout to build: a centred title "
        "block, a progress rule, four stat cards and the weekday histogram, with the cover dropped "
        "so nothing waits on cover generation.",
        sleep_real(), sleep_proposed())


# =========================================================================
# 11. Boot splash
# =========================================================================
def boot(theme, caption, version):
    f = Frame(theme, caption)
    f.rect(W // 2 - 60, 300, 120, 120)
    f.big(theme["name"], 0, 450, w=W, align="center")
    f.small(version, 0, 500, w=W, align="center")
    f.small("Xteink X4 Pro", 0, H - 60, w=W, align="center")
    return f


section("Boot splash", "Same splash geometry; the wordmark and the version line differ.",
        boot(CP, "CrossPoint 1.5.0", "1.5.0"), boot(LE, "Lector 0.27.0-x4pro", "0.27.0-x4pro"))


# =========================================================================
# Touch: control center + touch-sized chrome (X4 Pro only surfaces)
# =========================================================================
def control_center_cp():
    """Upstream's FrontlightPanelActivity as it landed in 8575b1427: a sheet hanging from
    the top edge, grabber on its bottom edge, 56 px slider pills, 84 px tiles, gap 16,
    side margin 16."""
    f = Frame(CP, "CrossPoint: control center (FrontlightPanelActivity, #3156)")
    # dimmed page behind the sheet
    f.add(div("r", 0, 0, W, H, "background:#fbfbf9;"))
    f.small("The Left Hand of Darkness", SIDE_PAD, H - 40)
    m = 16
    y = 30
    for label, pct, lamp in ((S("STR_BRIGHTNESS", "Brightness"), 0.6, True),
                             (S("STR_WARMTH", "Warmth"), 0.5, False)):
        f.small(label, m + 8, y)
        y += CP["small_lh"] + 6
        pill_w = W - m * 2 - (66 if lamp else 0)
        f.rect(m, y, pill_w, 56, extra="border-radius:28px;")
        f.rect(m, y, int(pill_w * pct), 56, fill=True, extra="border-radius:28px;")
        if lamp:
            f.rect(W - m - 56, y, 56, 56, extra="border-radius:28px;")
            f.ui("\u2600", W - m - 56, y + 14, w=56, align="center")
        y += 56 + 14
    y += 6
    tiles = [S("STR_NIGHT_MODE", "Night Mode"), S("STR_FORCE_REFRESH", "Force Refresh"),
             S("STR_PORTRAIT", "Portrait"), "Touch On"]
    states = [True, False, False, False]
    tw = (W - m * 2 - 16) // 2
    for i, (t, on) in enumerate(zip(tiles, states)):
        tx = m + (i % 2) * (tw + 16)
        ty = y + (i // 2) * (84 + 16)
        f.rect(tx, ty, tw, 84, fill=on, extra="border-radius:10px;")
        f.ui(t, tx, ty + (84 - CP["ui_lh"]) // 2, w=tw, align="center",
             color="#fff" if on else "#000")
    bottom = y + 2 * 84 + 16 + 20
    f.line(0, bottom, W, 2)
    f.add(div("r", W // 2 - 40, bottom - 12, 80, 5, "background:#000;border-radius:3px;"))
    return f


def control_center_le():
    """Proposed for this fork: same job, this fork's idiom — no FUI, square 1-bit chrome,
    Cozette labels, our own value bar, and the tile set extended with the rows Lector has
    and upstream does not."""
    f = Frame(LE, "Proposed Lector control center: same gesture, this fork's chrome")
    f.add(div("r", 0, 0, W, H, "background:#fbfbf9;"))
    f.small("The Left Hand of Darkness", SIDE_PAD, H - 40)
    m = SIDE_PAD
    y = 24
    f.small("14:32", m, y)
    f.small("87%  ·  0.27.0-x4pro", 0, y, w=W - m, align="right")
    y += LE["small_lh"] + 12
    for label, pct, val, lamp in ((S("STR_FRONTLIGHT", "Frontlight"), 0.6, "60", True),
                                  (S("STR_FRONTLIGHT_WARMTH", "Warmth"), 0.4, "40", False)):
        f.ui(label, m, y)
        f.ui(val, 0, y, w=W - m, align="right")
        y += LE["ui_lh"] + 6
        pill_w = W - m * 2 - (72 if lamp else 0)
        f.rect(m, y, pill_w, 56)
        f.rect(m, y, int(pill_w * pct), 56, fill=True)
        if lamp:
            f.rect(W - m - 60, y, 60, 56, fill=True)
            f.ui("ON", W - m - 60, y + (56 - LE["ui_lh"]) // 2, w=60, align="center", color="#fff")
        y += 56 + 16
    tiles = [(S("STR_NIGHT_MODE", "Night Mode"), True), (S("STR_FORCE_REFRESH", "Refresh"), False),
             (S("STR_PORTRAIT", "Portrait"), False), ("Touch On", False),
             (S("STR_WIFI_NETWORKS", "Wi-Fi"), False), (S("STR_STATS", "Stats"), False)]
    tw = (W - m * 2 - 12) // 2
    for i, (t, on) in enumerate(tiles):
        tx = m + (i % 2) * (tw + 12)
        ty = y + (i // 2) * (84 + 12)
        f.rect(tx, ty, tw, 84, fill=on)
        f.ui(t, tx, ty + (84 - LE["ui_lh"]) // 2, w=tw, align="center",
             color="#fff" if on else "#000")
    bottom = y + 3 * 84 + 2 * 12 + 20
    f.line(0, bottom, W, 3)
    f.add(div("r", W // 2 - 40, bottom - 14, 80, 6, "background:#000;"))
    f.small("swipe from top edge  ·  tap outside to close", 0, bottom + 14,
            w=W, align="center")
    return f


section("Control center (top drawer)",
        "This is the bar that pulls down from the top. Upstream landed it yesterday in "
        "8575b1427 as FrontlightPanelActivity: sheet from the top edge, grabber on its bottom "
        "edge, 56 px slider pills, 84 px tiles. This fork has the gesture plumbing already — "
        "MappedInputManager::wasMenuGesture() decodes a top-edge down-swipe (top 14% of the "
        "screen) — but nothing calls it and there is no panel to open, so today the gesture does "
        "nothing. The right frame is the panel to build in this fork's own chrome, with two extra "
        "tiles for surfaces Lector has and upstream does not.",
        control_center_cp(), control_center_le())


def touch_list(caption, touch):
    """Settings list at today's metrics vs touch metrics."""
    f = Frame(LE, caption)
    top = LE["top_padding"]
    f.header(S("STR_SETTINGS_TITLE"))
    y = top + HEADER_H + VSPACE
    rows = [S("STR_SLEEP_SCREEN"), S("STR_QUICK_RESUME_TIMEOUT"), S("STR_FRONTLIGHT"),
            S("STR_FRONTLIGHT_BRIGHTNESS"), S("STR_REFRESH_FREQ"), S("STR_LANGUAGE")]
    vals = ["Cover", "5 min", S("STR_STATE_ON", "On"), "60", "10", "English"]
    row_h = 56 if touch else ROW_H
    for i, r in enumerate(rows):
        cur = y + i * row_h
        sel = (i == 2)
        if sel:
            f.rect(SIDE_PAD - 6, cur, W - (SIDE_PAD - 6) * 2, row_h, fill=True, stroke=False)
        col = "#fff" if sel else "#000"
        f.ui(r, SIDE_PAD, cur + (row_h - LE["ui_lh"]) // 2, color=col)
        f.ui(vals[i], SIDE_PAD, cur + (row_h - LE["ui_lh"]) // 2, color=col,
             align="right", w=W - SIDE_PAD * 2)
        if not sel:
            f.add(div("r", SIDE_PAD, cur + row_h - 1, W - SIDE_PAD * 2, 1,
                      "background:%s;" % ("#000" if touch else "transparent")))
        # touch-target overlay
        f.add(div("r", SIDE_PAD - 6, cur, W - (SIDE_PAD - 6) * 2, row_h,
                  "outline:1px dashed %s;outline-offset:-1px;" % ("#2a7" if touch else "#c33")))
    f.small("row %d px  ·  %s" % (row_h, "clears the 48 px floor" if touch else "below the 48 px floor"),
            SIDE_PAD, y + len(rows) * row_h + 12)
    if touch:
        # tappable hint band: full-width targets instead of 106 px boxes
        for i, lab in enumerate([S("STR_BACK"), S("STR_SELECT"), S("STR_DIR_UP"), S("STR_DIR_DOWN")]):
            bx = i * (W // 4)
            f.rect(bx, H - 56, W // 4, 56)
            f.ui(lab, bx, H - 56 + (56 - LE["ui_lh"]) // 2, w=W // 4, align="center")
    else:
        f.hints([S("STR_BACK"), S("STR_SELECT"), S("STR_DIR_UP"), S("STR_DIR_DOWN")])
    return f


section("Touch targets on a list",
        "Every list in the firmware draws 30 px rows (listRowHeight) and 106 x 40 px hint boxes. "
        "On the X4 Pro's GT911 glass those are under the 48 px finger-target floor — dashed red "
        "on the left. The right frame is the same screen at touch metrics: 56 px rows with a rule "
        "between them, and the hint band as four full-width 56 px targets. The metrics already "
        "have a hook for this (UITheme::metricsForTouch), so it is a per-board metrics table, "
        "not a rewrite of every activity.",
        touch_list("Lector today: 30 px rows, 106 x 40 hints", False),
        touch_list("Proposed: 56 px rows, full-width 56 px hint band", True))


# =========================================================================
# Fork-only screens (no CrossPoint twin)
# =========================================================================
def only_list(caption, title, rows, vals=None, hints=None, selected=0, heads=None, note_rows=None):
    f = Frame(LE, caption)
    top = LE["top_padding"]
    f.header(title)
    y = top + HEADER_H + VSPACE
    if note_rows:
        for i, ln in enumerate(note_rows):
            f.small(ln, SIDE_PAD, y + i * (LE["small_lh"] + 4))
        y += len(note_rows) * (LE["small_lh"] + 4) + VSPACE
    f.list_rows(rows, y, selected=selected, values=vals, headings=heads)
    f.hints(hints or [S("STR_BACK"), S("STR_SELECT"), S("STR_DIR_UP"), S("STR_DIR_DOWN")])
    return f


def stats_screen():
    f = Frame(LE, "Lector only: BookStatsActivity")
    top = LE["top_padding"]
    f.header(S("STR_STATS_BOOK", "Book"))
    y = top + HEADER_H + VSPACE
    f.ui("The Left Hand of Darkness", SIDE_PAD, y)
    f.small("Ursula K. Le Guin", SIDE_PAD, y + LE["ui_lh"] + 2)
    y += LE["ui_lh"] + LE["small_lh"] + 16
    pairs = [(S("STR_STATS_TIME_READ", "Time read"), "4 h 12 m"),
             (S("STR_STATS_AVG_SESSION"), "22 min"),
             (S("STR_STATS_EST_FINISH"), "~5 h 10 m"),
             (S("STR_STATS_COMPLETED"), "42%"),
             (S("STR_STATS_BEST"), "Evening"),
             (S("STR_STATS_DAY_OF_WEEK"), "Sunday")]
    for i, (k, v) in enumerate(pairs):
        f.ui(k, SIDE_PAD, y + i * ROW_H)
        f.ui(v, SIDE_PAD, y + i * ROW_H, align="right", w=W - SIDE_PAD * 2)
    y += len(pairs) * ROW_H + 20
    # weekday bars
    days = [S("STR_STATS_MON", "Mon"), "Tue", "Wed", "Thu", S("STR_STATS_FRI", "Fri"), "Sat", "Sun"]
    heights = [30, 48, 22, 66, 40, 84, 58]
    bw = (W - SIDE_PAD * 2) // 7
    for i, (d, hgt) in enumerate(zip(days, heights)):
        bx = SIDE_PAD + i * bw
        f.rect(bx + 4, y + 90 - hgt, bw - 12, hgt, fill=True, stroke=False)
        f.small(d, bx, y + 96, w=bw - 8, align="center")
    f.hints([S("STR_BACK"), "", S("STR_STATS_ALL_BOOKS"), S("STR_STATS_MORE")])
    return f


ONLY = [
    ("Reading stats", "Per-book reading time, estimated finish, best time of day and a weekday "
                      "histogram. No upstream equivalent.", stats_screen()),
    ("Quotes viewer", "Grabbed quotes for the open book, hold to delete.",
     only_list("Lector only: QuotesViewerActivity", S("STR_QUOTES", "Quotes"),
               ["“Light is the left hand of darkness…”", "“He was a man of the frozen world.”",
                "“I am a woman of peace, and I have no…”"], selected=0,
               hints=[S("STR_BACK"), S("STR_HOLD_TO_DELETE"), S("STR_DIR_UP"), S("STR_DIR_DOWN")])),
    ("Reading presets", "Named looks saved from the current text settings, applied in one step.",
     only_list("Lector only: ReaderPresetsActivity", S("STR_READING_THEMES"),
               [S("STR_SAVE_CURRENT_LOOK"), "Night reading", "Daylight", "Big text"],
               vals=["", S("STR_CURRENT"), "", ""], selected=1,
               hints=[S("STR_BACK"), S("STR_APPLY"), S("STR_DIR_UP"), S("STR_DIR_DOWN")])),
    ("Steal look", "Copy the text settings of another book that already looks right.",
     only_list("Lector only: StealLookActivity", S("STR_STEAL_LOOK"),
               ["Blood Meridian", "Piranesi", "Dune", "The Dispossessed"], selected=1)),
    ("Installed fonts", "SD-card fonts with their size count and byte cost, send one to another reader.",
     only_list("Lector only: InstalledFontsActivity", S("STR_INSTALLED_FONTS"),
               ["Vollkorn", "XCharter", "OpenDyslexic", "ChareInk"],
               vals=[S("STR_SELECTED"), "4 sizes, 210 KB", "4 sizes, 180 KB", "3 sizes, 96 KB"],
               selected=0,
               hints=[S("STR_BACK"), S("STR_SELECT"), S("STR_SEND_FONT"), S("STR_DELETE")])),
    ("Lookup history", "Every dictionary lookup, re-openable.",
     only_list("Lector only: DictionaryHistoryActivity", S("STR_LOOKUP_HISTORY"),
               ["shifgrethor", "kemmer", "glacier", "obsidian", "vermilion"], selected=0,
               hints=[S("STR_BACK"), S("STR_SELECT"), S("STR_CLEAR_HISTORY"), S("STR_DIR_DOWN")])),
    ("Nearby: send a file", "Reader-to-reader file and font transfer over the local radio.",
     only_list("Lector only: NearbyFileTransferActivity", S("STR_NEARBY_TRANSFER"),
               ["Diogo's X3", "Reader-4F2A"], vals=[S("STR_NEARBY_SENDING_FILE"), ""],
               selected=0, note_rows=[S("STR_NEARBY_LOOKING_FOR_READERS"),
                                      S("STR_NEARBY_FILE_OF_FORMAT").replace("%u", "1")],
               hints=[S("STR_BACK"), S("STR_NEARBY_ACCEPT"), S("STR_NEARBY_DECLINE"), ""])),
    ("Nearby: sync position", "Trade reading positions with another reader on the same book.",
     only_list("Lector only: NearbyPositionSyncActivity", S("STR_NEARBY_SYNC"),
               [S("STR_NEARBY_THEIRS") + "  ·  312", S("STR_NEARBY_MINE") + "  ·  288"],
               selected=0, note_rows=[S("STR_NEARBY_FOUND"), S("STR_NEARBY_FURTHER_AHEAD")],
               hints=[S("STR_BACK"), S("STR_NEARBY_TAKE_THEIRS"), S("STR_NEARBY_SEND_MINE"), ""])),
    ("Pop-up items", "Chooses what the button-bound menu pop-up contains.",
     only_list("Lector only: PopupItemsActivity", S("STR_POPUP_ITEMS"),
               [S("STR_BOOKMARKS"), S("STR_GRAB_QUOTE"), S("STR_GO_TO_PERCENT"),
                S("STR_TEXT_SETTINGS"), S("STR_FOOTNOTES")],
               vals=[S("STR_STATE_ON", "On"), S("STR_STATE_ON", "On"), S("STR_STATE_OFF", "Off"),
                     S("STR_STATE_ON", "On"), S("STR_STATE_OFF", "Off")],
               selected=1,
               hints=[S("STR_BACK"), S("STR_TOGGLE"), S("STR_DIR_UP"), S("STR_DIR_DOWN")])),
    ("Clean storage", "Deletes generated caches and orphaned files, with the warning shown first.",
     only_list("Lector only: CleanStorageActivity", S("STR_CLEAN_STORAGE"),
               [S("STR_CLEAN_BUTTON"), S("STR_CANCEL")], selected=1,
               note_rows=[S("STR_CLEAN_STORAGE_WARNING_1"), S("STR_CLEAN_STORAGE_WARNING_2"),
                          S("STR_CLEAN_STORAGE_WARNING_3")],
               hints=[S("STR_BACK"), S("STR_SELECT"), "", ""])),
]


def reader_quote():
    f = Frame(LE, "Lector only: QuoteSelectActivity")
    lh = 34
    lines = ["She walked the long corridor twice before",
             "she understood that the doors were not",
             "doors at all, and that the hall had no end",
             "she could reach by walking."]
    for i, ln in enumerate(lines):
        f.add(text(ln, 28, 120 + i * lh, 32, lh, "Serif"))
    # selection band over the second and third line
    f.add(div("r", 24, 120 + lh - 2, W - 60, 2, "background:#000;"))
    f.add(div("r", 24, 120 + 3 * lh - 2, W - 120, 2, "background:#000;"))
    f.add(div("r", 24, 120 + lh, 8, 2 * lh, "background:#000;"))
    f.small("2 lines selected", SIDE_PAD, H - HINTS_H - LE["small_lh"] - 10)
    f.hints([S("STR_BACK"), S("STR_SELECT"), S("STR_DIR_LEFT"), S("STR_DIR_RIGHT")])
    return f


def pxc_viewer():
    f = Frame(LE, "Lector only: PxcViewerActivity")
    f.rect(40, 60, W - 80, 620)
    f.small("wallpaper.pxc", 40, 360, w=W - 80, align="center", color="#888")
    f.small("3 / 48  ·  favourite", 0, 700, w=W, align="center")
    f.hints([S("STR_BACK"), S("STR_FAV"), S("STR_SLEEP_MOVE_TO_SLEEP"), S("STR_DELETE")])
    return f


def low_battery():
    f = Frame(LE, "Lector only: LowBatteryNoticeActivity")
    f.rect(W // 2 - 70, 280, 140, 70)
    f.rect(W // 2 + 70, 300, 8, 30, fill=True, stroke=False)
    f.rect(W // 2 - 64, 286, 16, 58, fill=True, stroke=False)
    f.big(S("STR_BATTERY_LOW"), 0, 400, w=W, align="center")
    f.small("Charge before the next long read", 0, 450, w=W, align="center")
    f.hints(["", S("STR_DONE"), "", ""])
    return f


ONLY.insert(2, ("Quote select",
             "Word-by-word selection inside the page before a quote is saved.", reader_quote()))
ONLY.append(("Wallpaper viewer", "Browse the .pxc sleep faces on the card, favourite or delete one.",
             pxc_viewer()))
ONLY.append(("Low battery notice", "Shown once when the pack crosses the low threshold.",
             low_battery()))

for title, note, frame in ONLY:
    section(title, note, only=frame)


# =========================================================================
# HTML
# =========================================================================
def font_face(name, path, weight="normal"):
    b64 = base64.b64encode(open(path, "rb").read()).decode()
    return (f"@font-face{{font-family:{name};font-weight:{weight};"
            f"src:url(data:font/woff2;base64,{b64}) format('woff2');}}")


faces = "".join([
    font_face("Cozette", f"{FONT_DIR}/Cozette-Regular.woff2"),
    font_face("CozetteB", f"{FONT_DIR}/Cozette-Bold.woff2"),
    font_face("Ubuntu", f"{FONT_DIR}/Ubuntu-Medium.woff2"),
    font_face("UbuntuB", f"{FONT_DIR}/Ubuntu-Bold.woff2"),
    font_face("NotoSans", f"{FONT_DIR}/NotoSans-Regular.woff2"),
])

body = []
for i, s in enumerate(SECTIONS, 1):
    body.append(f'<section><h2>{i}. {esc(s["title"])}</h2><p class="note">{esc(s["note"])}</p>')
    body.append('<div class="pair">')
    if s["only"] is not None:
        body.append('<div class="col only">' + s["only"].render() + '</div>')
    else:
        body.append('<div class="col">' + s["cp"].render() + '</div>')
        body.append('<div class="col">' + s["le"].render() + '</div>')
    body.append("</div></section>")

paired = sum(1 for s in SECTIONS if s["only"] is None)
only_n = sum(1 for s in SECTIONS if s["only"] is not None)

HTML = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Xteink X4 Pro — CrossPoint vs Lector</title>
<style>
{faces}
*{{box-sizing:border-box;}}
body{{margin:0;padding:24px 16px 64px;background:#e9e7e2;color:#111;
     font-family:ui-sans-serif,system-ui,-apple-system,sans-serif;}}
h1{{font-size:22px;margin:0 0 6px;}}
.lede{{max-width:900px;font-size:14px;line-height:1.5;color:#333;margin:0 0 28px;}}
.lede code{{background:#dcd9d3;padding:1px 4px;border-radius:3px;}}
section{{margin:0 auto 44px;max-width:1060px;}}
h2{{font-size:17px;margin:0 0 4px;}}
.note{{font-size:13px;line-height:1.5;color:#444;margin:0 0 14px;max-width:900px;}}
.pair{{display:flex;gap:24px;flex-wrap:wrap;}}
.col{{flex:0 0 auto;}}
.frame{{width:{W}px;}}
.screen{{position:relative;box-sizing:content-box;width:{W}px;height:{H}px;background:#fbfbf9;
        border:10px solid #2b2b2b;border-radius:14px;overflow:hidden;
        box-shadow:0 6px 18px rgba(0,0,0,.18);}}
.screen *{{position:absolute;}}
.t{{white-space:pre;overflow:hidden;-webkit-font-smoothing:none;font-smooth:never;}}
.cap{{font-size:12px;color:#555;margin-top:8px;text-align:center;}}
@font-face{{font-family:Serif;src:local('Georgia'),local('Times New Roman');}}
@media (max-width:1120px){{
  .pair{{gap:14px;}}
  .frame{{width:{int(W*0.62)}px;}}
  .screen{{transform:scale(.62);transform-origin:top left;
          margin-bottom:{-int(H*0.38)}px;}}
}}
</style></head>
<body>
<h1>Xteink X4 Pro — CrossPoint look vs Lector look</h1>
<p class="lede">Every frame is 480&times;800 device pixels, the X4 Pro's 800&times;480 panel driven
portrait. Geometry is taken from <code>BaseMetrics::values</code> in each tree and from
<code>BaseTheme::drawButtonHints</code> (hint boxes 106&times;40 at x = 25/130/245/350). The UI text
uses the real fonts each firmware ships: <b>Ubuntu Medium</b> at 20.8&nbsp;px for CrossPoint,
<b>Cozette</b> at 25&nbsp;px for Lector, both being the converted ppem
(<code>size &times; 150 / 72</code>) of the generated font headers. Body text stands in with a serif;
the reader font is user-chosen on both. {paired} paired screens, then {only_n} screens that exist
only in this fork.</p>
{''.join(body)}
</body></html>
"""

os.makedirs(os.path.dirname(OUT), exist_ok=True)
open(OUT, "w").write(HTML)
print(OUT, os.path.getsize(OUT), "bytes;", paired, "pairs;", only_n, "fork-only")
