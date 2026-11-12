# File Formats

These formats describe the SD-card cache files under `/.crosspoint/epub_<hash>/`.
All POD fields are written in the ESP32 little-endian representation used by
`Serialization.h`; strings are length-prefixed UTF-8.

## `book.bin`

### Version 13

The serialized layout is unchanged. Version 13 forces one TOC re-parse so an EPUB 3
`<nav epub:type="page-list">` is read: its entries are written beside `book.bin` as
`pages.txt`, one `label<TAB>href` line per printed page (`147\tOEBPS/c3.xhtml#p147`),
the href already resolved against the nav document's folder. The file exists only when
the book has a page list; Go to Page scans it for the typed label.

### Version 12

`book.bin` stores EPUB metadata plus lookup tables for spine and TOC entries.
The current firmware writes this version from `BookMetadataCache`.

Version 12 leaves a TOC entry whose href resolves to no spine item out of the
cache entirely, rather than storing it with `spineIndex = -1`. The serialized
layout is unchanged; older caches hold rows that close the chapter picker
without moving the reader.

Version 11 resolves a TOC target by file name when its exact path is not in the
spine, so a TOC document sitting in another folder still finds its chapter. The
serialized layout is unchanged; caches built before it hold the `-1` those rows
used to get.

Version 10 was upstream's ambiguous-guide fix. This fork's version 9 added the
`description` field to `Metadata`.

ImHex pattern:

```c++
import std.mem;
import std.string;
import std.core;

#define EXPECTED_VERSION 13
#define MAX_STRING_LENGTH 65535

struct String {
    u32 length [[hidden, comment("String byte length")]];
    if (length > MAX_STRING_LENGTH) {
        std::warning(std::format("Unusually large string length: {} bytes", length));
    }
    char data[length] [[comment("UTF-8 string data")]];
} [[sealed, format("format_string"), comment("Length-prefixed UTF-8 string")]];

fn format_string(String s) {
    return s.data;
};

struct Metadata {
    String title [[comment("Book title")]];
    String author [[comment("Book author")]];
    String language [[comment("Book language code")]];
    String coverItemHref [[comment("Path to cover image")]];
    String textReferenceHref [[comment("Path to guided first text reference")]];
    String description [[comment("Book description, shown on the Book Info screen")]];
};

struct SpineEntry {
    String href [[comment("Resource path")]];
    u32 cumulativeSize [[comment("Cumulative uncompressed spine size through this entry")]];
    s16 tocIndex [[comment("Index into TOC, or inherited/previous TOC index when no direct entry exists")]];
};

struct TocEntry {
    String title [[comment("Chapter/section title")]];
    String href [[comment("Resource path")]];
    String anchor [[comment("Fragment identifier")]];
    u8 level [[comment("Nesting level")]];
    s16 spineIndex [[comment("Index into spine (-1 if none)")]];
};

struct BookBin {
    u8 version;
    if (version != EXPECTED_VERSION) {
        std::error(std::format("Unsupported version: {} (expected {})", version, EXPECTED_VERSION));
    }

    u32 lutOffset [[comment("Offset to lookup tables")]];
    u16 spineCount;
    u16 tocCount;

    Metadata metadata;

    u32 currentOffset = $;
    if (currentOffset != lutOffset) {
        std::warning(std::format("LUT offset mismatch: expected 0x{:X}, got 0x{:X}", lutOffset, currentOffset));
    }

    u32 spineLut[spineCount] [[comment("Spine entry offsets")]];
    u32 tocLut[tocCount] [[comment("TOC entry offsets")]];

    SpineEntry spines[spineCount];
    TocEntry toc[tocCount];
};

BookBin book @ 0x00;

u32 fileSize = std::mem::size();
u32 parsedSize = $;
if (parsedSize != fileSize) {
    std::warning(std::format("Unparsed data detected: {} bytes remaining at offset 0x{:X}", fileSize - parsedSize, parsedSize));
}
```

## `section.bin`

### Version 87

Layout fix; the binary layout is unchanged (upstream #4431). A TrueType advance is read at
1/64 px and truncated to the 12.4 `advanceX`, so only the renderer's pixel snap rounds it
(it was rounded to 1/16 px first, which could push a width up a pixel). Bitmap fonts and
`.cpfont` files are unchanged. The partial version is 195.

### Version 86

Layout fix; the binary layout is unchanged. `<hgroup>` is a block (CrossInk cb58ab50d), so
a heading group gets the book's margins, and a subtitle in it keeps the group's alignment
when the reader overrides paragraph alignment. A container's bottom margin, such as a chapter
wrapper's, now comes before the next paragraph instead of after it (CrossInk 63a682578). The
partial version is 196.

### Version 85

Adds a `bool fillPages` after `characterSpacing` in the header (upstream #4247). On, no
line is carried to the next page to avoid a lone first or last line or a heading at the
page bottom; page breaks the book asks for still apply. Changing it rebuilds the section.
The partial version is 197.

### Version 84

Layout fix; the binary layout is unchanged. A Unicode space the font lacks (U+00A0,
U+2000 to U+200A, U+202F, U+205F, U+3000) takes the plain space's glyph and width, and a
missing zero-width character (U+200B to U+200F, U+2060 to U+2064, U+FEFF) takes none.
Both used to draw the replacement box. Lines holding one lay out again. The partial
version is 198.

### Version 82

Layout fix; the binary layout is unchanged. A character missing from an SD TrueType font now
draws the font's .notdef box and takes its width, where it used to vanish (upstream #4256).
Lines holding one lay out again. The partial version is 200.

### Version 81

Layout fix; the binary layout is unchanged. Word Expansion counts the letters it spaces the
way drawing steps them: small caps keep ligature pairs as separate letters, and the count runs
over the shaped visual text. Justified lines holding such words lay out again. The partial
version is 201.

### Version 80

Layout fixes; the binary layout is unchanged. Text running straight into a block or heading
start stays its own word. Justification counts a literal space only when it has a continuation
after it, and a no-break space carried to the start of a wrapped line is dropped.
`font-weight: inherit` and `font-style: inherit` keep the parent's emphasis, which moves the CSS
rules cache to version 19. SD font word widths snap per glyph, as drawing does. The partial
version is 202.

### Version 79

Small images (no taller than two text lines and narrower than the column, unless the book
sets `display: block`) are laid out inside the text line. Each is stored as an ordinary
`TAG_PageImage` beside the line's `TAG_PageLine`; the binary layout is unchanged. A line
holding one is as tall as its tallest image, with text and images sharing the bottom edge.
A line of images alone is centred. The partial version is 203.

### Version 78

Publisher styling (upstream #3891). Each TextBlock writes a signed 32-bit `fontId` after
`letterSpacing`: the renderer font the line was laid out in, or 0 for the section font. Then
a `hasWordFonts` byte; when set, three signed 32-bit font ids and one slot byte per word
follow: 0 draws the word in the block's font, `n` in font `n - 1`; bit 0x80 marks CSS small
caps (the stored text keeps its case). Mixed-size words share the tallest font's baseline.

Pages gain two element types. `TAG_PageDropCap` (4) is an initial letter scaled up from a
font glyph: position (`yPos` is the baseline of the last spanned line), font id, scale in
1/256ths, style and up to 12 bytes of UTF-8. `TAG_PageBorderBox` (5) is a CSS border and/or
shade around one page's slice of a block: position, size, width and style per edge (a side
split off by a page break has width 0), and a shade flag. Tables plan their columns, and a
paragraph's lone first or last line moves to the next page. The partial version is 204.

### Version 77

Printed-page markers (`<span epub:type="pagebreak" id=…>` or `role="doc-pagebreak"`)
enter the anchor map; ids on other spans are still skipped. The binary layout is unchanged;
the derived partial version changes from 206 to 205.

Versions 67 and older, including the full version 53 layout every later version builds on:
[section-bin-history.md](section-bin-history.md).
