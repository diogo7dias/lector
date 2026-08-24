#!/usr/bin/env python3
"""Renders the FreeInkUI list-look variants for the X4 Pro as one HTML page.

Every frame is the real geometry: a 480x800 panel, the 14 px top padding and 45 px
header BaseMetrics carries, 30 px rows, a 56 px touch hint band, and Cozette at the
UI_10 pixel size the firmware ships. Only the list tokens change between frames, so
what differs on screen is exactly what differs in ThemeMetrics.
"""
import base64
import pathlib

FONT_DIR = pathlib.Path("/tmp/mock/fonts")
OUT = pathlib.Path(__file__).with_name("fui-list-variants.html")

W, H = 480, 800
TOP_PAD, HEADER_H, HINT_H = 14, 45, 56
ROW_H, SIDE_PAD = 30, 20
FONT_PX = 25


def font_face(name, path, weight=400):
    b64 = base64.b64encode(pathlib.Path(path).read_bytes()).decode()
    return (f"@font-face{{font-family:{name};font-weight:{weight};"
            f"src:url(data:font/woff2;base64,{b64}) format('woff2');}}")


ROWS = [
    ("Reading Font", "Bookerly"),
    ("Font Size", "10"),
    ("Line Spacing", "128%"),
    ("Margins", "24"),
    ("Text Alignment", "Justified"),
    ("Paragraph Spacing", "50%"),
    ("Hyphenation", "ON"),
    ("Text Anti-Aliasing", "OFF"),
    ("Images", "Display"),
    ("Paperback Look", "ON"),
    ("Night Mode", "OFF"),
    ("Status Bar", "ON"),
    ("Progress Bar", "Slim"),
    ("Orientation", "Portrait"),
    ("Side Buttons", "Prev / Next"),
    ("Touch Reader Controls", "Swipe"),
    ("Show Reader Menu", "Tap"),
    ("Short Power Click", "Ignore"),
    ("Wake Hold", "Normal"),
    ("Time To Sleep", "15"),
    ("Sleep Screen", "Wallpaper"),
    ("Language", "English"),
    ("Firmware", "0.27.0"),
]
SELECTED = 3


def frame(title, note, *, gap, radius, inset, side_pad, selection, scroll_side, row_h=ROW_H):
    """One panel. selection is 'fill', 'pill' or 'outline'."""
    list_top = TOP_PAD + HEADER_H + 10
    list_h = H - list_top - HINT_H - 10
    rows_html = []
    y = list_top
    for i, (label, value) in enumerate(ROWS):
        if y + row_h > list_top + list_h:
            break
        sel = i == SELECTED
        left = inset + side_pad
        right = inset + side_pad
        box = ""
        if radius > 0 and not sel:
            box = (f"<div class='card' style='left:{inset}px;top:{y}px;width:{W - inset * 2}px;"
                   f"height:{row_h}px;border-radius:{radius}px'></div>")
        if sel:
            bx = inset if selection != "pill" else inset + 6
            bw = W - bx * 2
            if selection == "outline":
                box = (f"<div class='sel out' style='left:{bx}px;top:{y}px;width:{bw - 2}px;"
                       f"height:{row_h - 2}px;border-radius:{radius}px'></div>")
            else:
                box = (f"<div class='sel' style='left:{bx}px;top:{y}px;width:{bw}px;"
                       f"height:{row_h}px;border-radius:{radius}px'></div>")
        ink = "#fff" if sel and selection != "outline" else "#000"
        rows_html.append(
            f"{box}<div class='row' style='top:{y}px;height:{row_h}px'>"
            f"<span class='lbl' style='left:{left}px;color:{ink}'>{label}</span>"
            f"<span class='val' style='right:{right}px;color:{ink}'>{value}</span></div>")
        y += row_h + gap

    sx = 6 if scroll_side == "left" else W - 6 - 4
    scroll = (f"<div class='track' style='left:{sx}px;top:{list_top}px;height:{list_h}px'></div>"
              f"<div class='thumb' style='left:{sx}px;top:{list_top + 20}px;height:{int(list_h * 0.45)}px'></div>")

    hints = "".join(
        f"<div class='hint' style='left:{i * (W // 4)}px;width:{W // 4}px'>{t}</div>"
        for i, t in enumerate(["BACK", "SELECT", "UP", "DOWN"]))

    return f"""
    <figure>
      <figcaption><b>{title}</b><span>{note}</span></figcaption>
      <div class='panel'>
        <div class='header'>SETTINGS</div>
        {''.join(rows_html)}
        {scroll}
        <div class='hints'>{hints}</div>
      </div>
    </figure>"""


CSS = f"""
{font_face('Cozette', FONT_DIR / 'Cozette-Regular.woff2')}
{font_face('CozetteB', FONT_DIR / 'Cozette-Bold.woff2', 700)}
body{{background:#2b2b2b;color:#eee;font-family:system-ui,sans-serif;margin:24px}}
h1{{font-size:20px;margin:0 0 4px}}
p.lede{{color:#bbb;max-width:70ch;line-height:1.5;margin:0 0 24px}}
.grid{{display:flex;flex-wrap:wrap;gap:28px}}
figure{{margin:0}}
figcaption{{display:flex;flex-direction:column;gap:2px;margin-bottom:8px;font-size:13px}}
figcaption span{{color:#aaa;max-width:{W}px;line-height:1.4}}
.panel{{position:relative;width:{W}px;height:{H}px;background:#f7f5ef;color:#000;
  box-sizing:content-box;border:10px solid #111;border-radius:14px;overflow:hidden}}
.header{{position:absolute;left:0;top:{TOP_PAD}px;width:{W}px;height:{HEADER_H}px;
  display:flex;align-items:center;justify-content:center;
  font-family:CozetteB;font-size:{FONT_PX}px}}
.row{{position:absolute;left:0;width:{W}px;display:flex;align-items:center}}
.row span{{position:absolute;font-family:Cozette;font-size:{FONT_PX}px;line-height:1}}
.sel{{position:absolute;background:#000}}
.sel.out{{background:none;border:2px solid #000}}
.track{{position:absolute;width:4px;background:#d8d4c8}}
.thumb{{position:absolute;width:4px;background:#000}}
.hints{{position:absolute;left:0;bottom:0;width:{W}px;height:{HINT_H}px;border-top:2px solid #000}}
.hint{{position:absolute;top:0;height:{HINT_H}px;display:flex;align-items:center;justify-content:center;
  font-family:Cozette;font-size:19px;border-left:2px solid #000;box-sizing:border-box}}
.hint:first-child{{border-left:none}}
"""

FRAMES = [
    frame("A. Today", "What the firmware draws now, by hand: 30 px rows edge to edge, no gap, "
          "selection is a full-width filled bar, scroll indicator on the right.",
          gap=0, radius=0, inset=0, side_pad=SIDE_PAD, selection="fill", scroll_side="right"),
    frame("B. Hosted, same tokens", "The same screen drawn by FreeInkUI with the tokens seeded to "
          "match A. This is what you get if we change nothing.",
          gap=0, radius=0, inset=0, side_pad=SIDE_PAD, selection="fill", scroll_side="right"),
    frame("C. Carded", "FreeInkUI's own shape: rows inset from the edges with a 4 px gap and rounded "
          "corners, selection a rounded pill inside the row.",
          gap=4, radius=6, inset=10, side_pad=12, selection="pill", scroll_side="right"),
    frame("D. Outlined selection", "Flat rows as in A, but the cursor is an outline rather than a "
          "filled bar, so the row's own text stays black on paper.",
          gap=0, radius=0, inset=0, side_pad=SIDE_PAD, selection="outline", scroll_side="right"),
    frame("E. Airy", "A gap between rows and no inset: the list breathes without becoming cards. "
          "Fewer rows per screen (this is the real cost).",
          gap=8, radius=0, inset=0, side_pad=SIDE_PAD, selection="fill", scroll_side="right"),
    frame("F. Scrollbar left", "A on the right-hand side, with the scroll indicator moved to the "
          "left edge instead.",
          gap=0, radius=0, inset=0, side_pad=SIDE_PAD, selection="fill", scroll_side="left"),
]

HTML = f"""<!doctype html><meta charset="utf-8">
<title>Lector — FreeInkUI list looks (X4 Pro)</title>
<style>{CSS}</style>
<h1>FreeInkUI list looks — X4 Pro, real geometry, real UI font</h1>
<p class="lede">480x800 panel, 14 px top padding, 45 px header, 30 px rows, 56 px touch hint band,
Cozette at the UI_10 size the firmware ships. Only the list tokens differ between frames:
row gap, row radius, list inset, side padding, selection style, scroll side. Pick one, or mix
(for example C's inset with A's selection).</p>
<div class="grid">{''.join(FRAMES)}</div>
"""

OUT.write_text(HTML)
print(OUT, len(HTML))
