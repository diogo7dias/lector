# File Formats

These formats describe the SD-card cache files under `/.crosspoint/epub_<hash>/`.
All POD fields are written in the ESP32 little-endian representation used by
`Serialization.h`; strings are length-prefixed UTF-8.

## `book.bin`

### Version 14

The serialized layout is unchanged. A `.` path segment is now dropped when an href is
resolved, so `./Text/ch.xhtml` in the OPF becomes `OEBPS/Text/ch.xhtml`, the zip entry's
own name. Caches built before it hold `OEBPS/./Text/ch.xhtml`, which matches no entry.

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

### Version 93

Layout only; the binary layout is unchanged. A `<blockquote>` whose CSS sets no side margin
is inset 1 em from that side, shrunk so nested quotes keep half the column, so chapters with
quotes wrap differently. The partial version is 189.

### Version 92

Layout only; the binary layout is unchanged. Fill Pages is always on: no line is carried to
the next page to avoid a lone first or last line or a heading at the page bottom, so pages
end full. Page breaks the book asks for still apply. The partial version is 190.

### Version 90

Layout only; the binary layout is unchanged. A GIF `<img>` is laid out and drawn (first
frame) instead of left out of the page, so pages that carry one paginate differently. The
partial version is 192. The pixel cache tag is unchanged: no existing image decodes to other
pixels, and a GIF's cache is named like any other (`<n>.t2.pxc`).

### Version 89

Lean typography pass; the header and the text line change. The header is 44 bytes (was 54)
and now holds, in order: `fontId`, `lineCompression`, `extraParagraphSpacing`, `wordSpacing`,
`paragraphAlignment`, viewport width and height, `embeddedStyle` (one byte, was two),
`imageRendering`, `focusReadingEnabled`, `guideDotsMode`, `paragraphIndentSpaces` (0 to 5
spaces, replacing the indent mode and percent), `characterSpacing` (whole pixels, -2 to 2,
was half pixels), the page count and the five LUT offsets. `paragraphSpacing`, kerning,
ligatures, link underline, book margins, heading page break, word expansion and fill pages
are gone from the header; kerning, ligatures, link underline and book margins are now always on. Each `TextBlock` loses
its letter-spacing byte. Word Spacing runs 50 to 200 and the paragraph indent has no
3-space floor, so lines lay out again. The partial version is 193. The per-book
`reader_override.bin` and the presets keep their layout and read old files; see
`ReaderPrefs.h` (version 27). The pixel cache tag is unchanged: Images: Black & White dithers
the cached 4-level pixels at draw time.

### Version 88

Path fix; the binary layout is unchanged. An image `src` with a `.` segment (`./img.jpg`)
resolves to the zip entry's own name, so the image is laid out and drawn instead of left out
of the page. The partial version is 194.

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

## Library index (`/.crosspoint/lector-library.idx`)

The list behind Home > Library: one entry per book on the card, in title order. Magic
`CLX1`, format version 2 (version 1 had no reading progress). It is not CrossPoint's `CLX1`
(different records, kept in `library.idx`), so a card moved between the two firmwares never makes them
rebuild each other's file. A file of another version, size or layout fails validation and is
rebuilt; nothing is migrated.

Little-endian, packed. Each section starts on a 512-byte boundary.

| Section | Offset | Content |
|---|---|---|
| header | 0 | 64 bytes, padded to 512 |
| records | 512 | `bookCount` x 32 bytes, in title order |
| names | `nameStart` | per record, in the same order: title, author, path (no terminators) |

Header (64 bytes): `char magic[4]`, `u8 formatVersion` (2), `u8 flags` (bit 0: more books on the
card than fit, so the list stops at the cap), `u16 bookCount`, `u32 recordStart` (512),
`u32 nameStart` (`recordStart + 32 * bookCount`, rounded up to 512), `u32 nameLen`,
`u32 selfSize` (`nameStart + nameLen`; must equal the real file size, so a cut file is
rejected), `u8 reserved[40]`.

Record (32 bytes): `u64 pathHash` (FNV-1a 64 of the path), `u32 nameOff` (from `nameStart`),
`u32 fileSize`, `u32 modified` (FAT date << 16 | FAT time, 0 when the card gives none),
`u8 titleLen`, `u8 authorLen`, `u8 pathLen`, `u8 percent` (reading progress 0 to 100, 255 when
never opened), `u32 readOrder` (the reading-session counter at the last exit, 0 when unknown),
`u8 reserved[4]`. A book's title, author and
path are the `titleLen`, `authorLen` and `pathLen` bytes at `nameStart + nameOff`. Bounds: title
128, author 96, path 255 bytes; a record whose text leaves the names section or exceeds
these is refused when read.

Row `k` is at `512 + 32k`, so a page of rows is one read of the records and one small read per
row for its text; nothing is loaded whole. The sort key is the title lower-cased in ASCII
(other bytes as they are), so non-ASCII titles follow ASCII ones.

Written by a walk of the card from `/`: books are files with an EPUB, XTC, TXT or MD extension,
not hidden, not `<book>_QUOTES.txt`, not in `System Volume Information` or `XTCache`, and not
under Lector's own root folders (`fonts`, `dictionaries`, `plugins`, `sleep`, `sleep pause`,
`sleep-overlay`, `stats`, `perf`). At most 6 folders deep. The build writes
`lector-library.idx.part`, then removes the old index and renames the part into place: a reset
leaves the previous index, or none between those two steps, never a half-written one under the
final name. `lector-library.stage` is the staging file (512 bytes per book) and is deleted
afterwards. A book keeps the title and author of the previous index when its path, size and
modification time are unchanged.

`percent` and `readOrder` come from each book's `percent.bin` at build time. Leaving a book, and
Mark Read or Mark Unread, rewrite that one 32-byte record in place (a record never crosses a
512-byte sector), so the list filters and sorts on progress without a rebuild.
