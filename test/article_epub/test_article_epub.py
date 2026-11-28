#!/usr/bin/env python3
"""Verify the Send Article client-side EPUB3 builder in FilesPage.html."""
import os
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile


def extract_builder_js(html_path: str) -> str:
    text = open(html_path, "r", encoding="utf-8").read()
    m = re.search(
        r"// --- SEND_ARTICLE_START ---(.*?)// --- SEND_ARTICLE_END ---",
        text,
        re.DOTALL,
    )
    if not m:
        raise AssertionError("SEND_ARTICLE_START/END markers missing from FilesPage.html")
    return m.group(1)


def main() -> int:
    node_bin, html_path, jszip_path = sys.argv[1:4]
    builder_js = extract_builder_js(html_path)

    with tempfile.TemporaryDirectory() as tmp:
        driver = os.path.join(tmp, "run.js")
        with open(driver, "w", encoding="utf-8") as f:
            f.write(f"const fs = require('fs');\n")
            f.write(f"const JSZip = require({jszip_path!r});\n")
            f.write(builder_js + "\n")
            f.write(
                """
(async () => {
  const cases = [
    {
      out: 'plain.epub',
      title: 'A Plain & Simple Note',
      author: 'Ada Lovelace',
      format: 'auto',
      body: 'First paragraph line 1\\nline 2.\\n\\nSecond paragraph with <angle> & quotes.'
    },
    {
      out: 'markdown.epub',
      title: 'Markdown Essay',
      author: 'Alan Turing',
      format: 'auto',
      body: '## Section One\\n\\nHello **bold** and *italic* and `code`.\\n\\n- item A\\n- item B\\n\\n> A wise quote\\n\\n```\\nlet x = 1 < 2;\\n```'
    },
    {
      out: 'html.epub',
      title: 'HTML Clip',
      author: '',
      format: 'auto',
      body: '<article><h2>Headline</h2><p>Body <script>alert(1)</script><strong>text</strong> &amp; link <a href="https://example.com">here</a><br>next line.</p></article>'
    }
  ];
  for (const c of cases) {
    const buf = await buildArticleEpub(c, 'uint8array');
    fs.writeFileSync(process.argv[2] + '/' + c.out, buf);
  }
  const fname = sanitizeArticleFilename('Bad/Name:With*Chars?.txt');
  if (fname !== 'Bad Name With Chars .txt.epub') {
    throw new Error('Unexpected sanitized filename: ' + fname);
  }
})().catch(e => { console.error(e); process.exit(1); });
"""
            )
        res = subprocess.run([node_bin, driver, tmp], capture_output=True, text=True)
        if res.returncode != 0:
            sys.stderr.write(res.stdout + res.stderr)
            return res.returncode

        for name, expected_title, expected_snippets, forbidden in [
            (
                "plain.epub",
                "A Plain & Simple Note",
                ["First paragraph line 1", "Second paragraph with <angle> & quotes."],
                [],
            ),
            (
                "markdown.epub",
                "Markdown Essay",
                ["Section One", "bold", "italic", "item A", "A wise quote", "let x = 1 < 2;"],
                [],
            ),
            (
                "html.epub",
                "HTML Clip",
                ["Headline", "Body", "text", "here", "next line."],
                ["alert(1)", "<script"],
            ),
        ]:
            epub_path = os.path.join(tmp, name)
            with zipfile.ZipFile(epub_path, "r") as zf:
                names = zf.namelist()
                assert names[0] == "mimetype", f"{name}: mimetype must be first entry, got {names}"
                info = zf.getinfo("mimetype")
                assert info.compress_type == zipfile.ZIP_STORED, f"{name}: mimetype must be stored uncompressed"
                assert zf.read("mimetype") == b"application/epub+zip"

                container = ET.fromstring(zf.read("META-INF/container.xml"))
                assert container.tag.endswith("container")

                opf = ET.fromstring(zf.read("OEBPS/content.opf"))
                assert opf.attrib.get("version") == "3.0"
                title_el = opf.find(".//{http://purl.org/dc/elements/1.1/}title")
                assert title_el is not None and title_el.text == expected_title

                nav = ET.fromstring(zf.read("OEBPS/nav.xhtml"))
                assert nav.tag.endswith("html")

                raw_article = zf.read("OEBPS/article.xhtml").decode("utf-8")
                article_root = ET.fromstring(raw_article)
                full_text = "".join(article_root.itertext())
                for snippet in expected_snippets:
                    assert snippet in full_text, f"{name}: missing {snippet!r} in {full_text!r}"
                for bad in forbidden:
                    assert bad not in raw_article, f"{name}: forbidden {bad!r} found in XHTML"

    print("ArticleEpubBuilder: plain, markdown, and html EPUB3 packages verified.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
