#!/usr/bin/env python3
"""Writes test/epubs/test_page_list.epub: an EPUB 3 whose nav carries a page-list.

Front matter is numbered i-iii, the three chapters 1-30; every printed page is a
<span epub:type="pagebreak"> inside the running text, the way publishers mark them.
"""
import os
import zipfile

ROMAN = ["i", "ii", "iii"]
OUT = os.path.join(os.path.dirname(__file__), "..", "test", "epubs", "test_page_list.epub")
PARA = ("The printed edition broke its pages here and there, wherever the type ran out, and the "
        "reader on the train only wanted to find page forty-two again. ")


def chapter(title, labels):
    body = [f"<h1>{title}</h1>"]
    for label in labels:
        body.append(f'<p><span epub:type="pagebreak" role="doc-pagebreak" id="page_{label}" title="{label}"/>'
                    f"<b>[printed page {label}]</b> " + PARA * 6 + "</p>")
    return ('<?xml version="1.0" encoding="UTF-8"?>\n<html xmlns="http://www.w3.org/1999/xhtml" '
            'xmlns:epub="http://www.idpf.org/2007/ops"><head><title>' + title + "</title></head><body>"
            + "".join(body) + "</body></html>")


def main():
    files = [("front.xhtml", "Front Matter", ROMAN)]
    for n in range(3):
        files.append((f"ch{n + 1}.xhtml", f"Chapter {n + 1}", [str(p) for p in range(n * 10 + 1, n * 10 + 11)]))

    toc = "".join(f'<li><a href="text/{f}">{t}</a></li>' for f, t, _ in files)
    pages = "".join(f'<li><a href="text/{f}#page_{l}">{l}</a></li>' for f, _, ls in files for l in ls)
    nav = ('<?xml version="1.0" encoding="UTF-8"?>\n<html xmlns="http://www.w3.org/1999/xhtml" '
           'xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Nav</title></head><body>'
           f'<nav epub:type="toc"><ol>{toc}</ol></nav>'
           f'<nav epub:type="page-list" hidden=""><ol>{pages}</ol></nav></body></html>')
    manifest = "".join(f'<item id="{f[:-6]}" href="text/{f}" media-type="application/xhtml+xml"/>' for f, _, _ in files)
    spine = "".join(f'<itemref idref="{f[:-6]}"/>' for f, _, _ in files)
    opf = ('<?xml version="1.0" encoding="UTF-8"?>\n<package xmlns="http://www.idpf.org/2007/opf" version="3.0" '
           'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
           '<dc:identifier id="id">lector-page-list-test</dc:identifier><dc:title>Page List Test</dc:title>'
           '<dc:creator>Lector Test Fixtures</dc:creator><dc:language>en</dc:language></metadata>'
           f'<manifest><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>{manifest}'
           f"</manifest><spine>{spine}</spine></package>")
    container = ('<?xml version="1.0"?>\n<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">'
                 '<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>'
                 "</rootfiles></container>")
    with zipfile.ZipFile(OUT, "w") as z:
        z.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", container, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/content.opf", opf, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/nav.xhtml", nav, compress_type=zipfile.ZIP_DEFLATED)
        for f, t, ls in files:
            z.writestr(f"OEBPS/text/{f}", chapter(t, ls), compress_type=zipfile.ZIP_DEFLATED)


if __name__ == "__main__":
    main()
