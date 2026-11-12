#!/usr/bin/env python3
"""Writes test/epubs/test_publisher_styling.epub: one chapter per publisher-styling feature.

Drop caps (::first-letter and a span), borders and shading, a fieldset, small caps,
CSS font sizes, descendant/child/#id selectors and a forced page break.
"""
import os
import zipfile

OUT = os.path.join(os.path.dirname(__file__), "..", "test", "epubs", "test_publisher_styling.epub")
TEXT = ("The lamp on the desk burned low while the rain kept time against the window, and the "
        "reader turned another page without noticing the hour. ")
CSS = """
p { margin: 0; text-indent: 1.2em; }
p.opening::first-letter { float: left; font-size: 3em; font-weight: bold; }
span.cap { float: left; font-size: 4em; }
div.boxed { border: 2px solid black; padding: 0.5em; margin: 1em 0; }
div.shaded { background-color: #ddd; padding: 0.5em; }
p.dashed { border-top: 1px dashed black; border-bottom: 3px double black; }
.sc { font-variant: small-caps; }
.big { font-size: 1.6em; }
.small { font-size: 0.75em; }
div.letter > p { font-style: italic; }
div.letter p.sign { text-align: right; }
#motto { font-weight: bold; text-align: center; }
h2.split { page-break-before: always; }
"""

CHAPTERS = [
    ("Drop Caps", f'<p class="opening">{TEXT * 4}</p><p><span class="cap">W</span>{TEXT * 3}</p>'),
    ("Borders and Shading",
     f'<div class="boxed"><p>{TEXT * 2}</p></div><div class="shaded"><p>{TEXT * 2}</p></div>'
     f'<p class="dashed">{TEXT}</p><fieldset><legend>Fieldset</legend><p>{TEXT}</p></fieldset>'),
    ("Small Caps and Sizes",
     f'<p><span class="sc">Small caps open this line</span> and plain text follows. {TEXT}</p>'
     f'<p>Mixed <span class="big">big words</span> and <span class="small">small words</span> share '
     f'one baseline. {TEXT}</p>'),
    ("Selectors and Breaks",
     f'<p id="motto">Centered bold motto by id</p><div class="letter"><p>{TEXT}</p>'
     f'<p class="sign">signed, right aligned</p></div><h2 class="split">After a forced break</h2>'
     f'<p>{TEXT * 2}</p>'),
]


def chapter(title, body):
    return ('<?xml version="1.0" encoding="UTF-8"?>\n<html xmlns="http://www.w3.org/1999/xhtml" '
            'xmlns:epub="http://www.idpf.org/2007/ops"><head><title>' + title +
            '</title><link rel="stylesheet" type="text/css" href="../style.css"/></head><body>'
            f"<h1>{title}</h1>{body}</body></html>")


def main():
    files = [(f"ch{n + 1}.xhtml", t, b) for n, (t, b) in enumerate(CHAPTERS)]
    toc = "".join(f'<li><a href="text/{f}">{t}</a></li>' for f, t, _ in files)
    nav = ('<?xml version="1.0" encoding="UTF-8"?>\n<html xmlns="http://www.w3.org/1999/xhtml" '
           'xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Nav</title></head><body>'
           f'<nav epub:type="toc"><ol>{toc}</ol></nav></body></html>')
    manifest = "".join(f'<item id="{f[:-6]}" href="text/{f}" media-type="application/xhtml+xml"/>' for f, _, _ in files)
    spine = "".join(f'<itemref idref="{f[:-6]}"/>' for f, _, _ in files)
    opf = ('<?xml version="1.0" encoding="UTF-8"?>\n<package xmlns="http://www.idpf.org/2007/opf" version="3.0" '
           'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
           '<dc:identifier id="id">lector-publisher-styling-test</dc:identifier>'
           '<dc:title>Publisher Styling Test</dc:title>'
           '<dc:creator>Lector Test Fixtures</dc:creator><dc:language>en</dc:language></metadata>'
           f'<manifest><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>'
           f'<item id="css" href="style.css" media-type="text/css"/>{manifest}'
           f"</manifest><spine>{spine}</spine></package>")
    container = ('<?xml version="1.0"?>\n<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">'
                 '<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>'
                 "</rootfiles></container>")
    with zipfile.ZipFile(OUT, "w") as z:
        z.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", container, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/content.opf", opf, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/nav.xhtml", nav, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/style.css", CSS, compress_type=zipfile.ZIP_DEFLATED)
        for f, t, b in files:
            z.writestr(f"OEBPS/text/{f}", chapter(t, b), compress_type=zipfile.ZIP_DEFLATED)


if __name__ == "__main__":
    main()
