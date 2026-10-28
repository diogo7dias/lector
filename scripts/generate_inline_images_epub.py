#!/usr/bin/env python3
"""Writes test/epubs/test_inline_images.epub: small images inside text lines.

A 20 px star sits mid-sentence and glued to a word, a 48 px ornament stands alone in its
paragraph, and a 300 px picture stays a centred block that breaks the paragraph.
"""
import io
import os
import zipfile

from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(__file__), "..", "test", "epubs", "test_inline_images.epub")
TEXT = "The lamp burned low while the rain kept time against the window. "


def png(size, draw):
    image = Image.new("L", (size, size), 255)
    draw(ImageDraw.Draw(image), size)
    out = io.BytesIO()
    image.save(out, "PNG")
    return out.getvalue()


def star(d, s):
    d.polygon([(s / 2, 0), (s * 0.62, s * 0.38), (s, s * 0.38), (s * 0.69, s * 0.6), (s * 0.8, s),
               (s / 2, s * 0.75), (s * 0.2, s), (s * 0.31, s * 0.6), (0, s * 0.38), (s * 0.38, s * 0.38)], fill=0)


def ornament(d, s):
    d.ellipse([4, 4, s - 4, s - 4], outline=0, width=4)
    d.line([0, s / 2, s, s / 2], fill=0, width=3)


def picture(d, s):
    for i in range(0, s // 2, 20):
        d.rectangle([i, i, s - i, s - i], outline=0, width=3)


BODY = (f'<p>{TEXT}A star <img src="../img/star.png" alt="star"/> sits mid-sentence, and one is glued'
        f' to this word<img src="../img/star.png" alt="star"/>here. {TEXT * 2}</p>'
        '<p><img src="../img/ornament.png" alt="ornament"/></p>'
        f'<p>{TEXT}Stars <img src="../img/star.png" alt="*"/><img src="../img/star.png" alt="*"/>'
        f'<img src="../img/star.png" alt="*"/> in a row. {TEXT}</p>'
        f'<p>{TEXT}<img src="../img/picture.png" alt="picture"/>{TEXT}</p>')


def main():
    chapter = ('<?xml version="1.0" encoding="UTF-8"?>\n<html xmlns="http://www.w3.org/1999/xhtml" '
               'xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Inline Images</title></head><body>'
               f"<h1>Inline Images</h1>{BODY}</body></html>")
    nav = ('<?xml version="1.0" encoding="UTF-8"?>\n<html xmlns="http://www.w3.org/1999/xhtml" '
           'xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Nav</title></head><body>'
           '<nav epub:type="toc"><ol><li><a href="text/ch1.xhtml">Inline Images</a></li></ol></nav></body></html>')
    images = {"star.png": png(20, star), "ornament.png": png(48, ornament), "picture.png": png(300, picture)}
    manifest = "".join(f'<item id="{n[:-4]}" href="img/{n}" media-type="image/png"/>' for n in images)
    opf = ('<?xml version="1.0" encoding="UTF-8"?>\n<package xmlns="http://www.idpf.org/2007/opf" version="3.0" '
           'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
           '<dc:identifier id="id">lector-inline-images-test</dc:identifier><dc:title>Inline Images Test</dc:title>'
           '<dc:creator>Lector Test Fixtures</dc:creator><dc:language>en</dc:language></metadata>'
           '<manifest><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>'
           f'<item id="ch1" href="text/ch1.xhtml" media-type="application/xhtml+xml"/>{manifest}</manifest>'
           '<spine><itemref idref="ch1"/></spine></package>')
    container = ('<?xml version="1.0"?>\n<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">'
                 '<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>'
                 "</rootfiles></container>")
    with zipfile.ZipFile(OUT, "w") as z:
        z.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", container, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/content.opf", opf, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/nav.xhtml", nav, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/text/ch1.xhtml", chapter, compress_type=zipfile.ZIP_DEFLATED)
        for name, data in images.items():
            z.writestr(f"OEBPS/img/{name}", data, compress_type=zipfile.ZIP_DEFLATED)


if __name__ == "__main__":
    main()
