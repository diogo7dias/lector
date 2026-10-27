#pragma once

// On-disk layout of a section cache file (.crosspoint/epub_<hash>/sections/<n>.bin),
// shared by the Section*.cpp files. Private to the Section implementation.

#include <cstdint>

namespace {
// v28: text decoration bits now include line-through in serialized wordStyles.
// v29: TextBlock word data stored as one flat arena (offset table + NUL-terminated
// text blob) instead of length-prefixed strings and per-field arrays.
// v30: Arabic shaping changed both drawing and measurement (getTextAdvanceX now
//      measures the shaped visual text); cached word positions from v29 no longer
//      match what drawText renders.
// v31: CJK words split at MAX_WORD_SIZE preserve continuation state.
// v32: ImageBlock serializes the book-internal source href after the cache path
//      (lazy extraction: images are header-probed at build time and extracted on
//      first render).
// v33: PageLine serializes a uint16 paragraphOrdinal (the visible-paragraph number
//      of a paragraph's first line) for the paragraph-numbers feature. Pagination is
//      byte-identical to v32; only the extra per-line field forces this one rebuild.
// v34: the render spec gains firstLineIndent (space-widths). It changes paragraph
//      first-line layout, so it joins the header's cache-key fields and forces a rebuild.
// v35: the render spec gains paragraphSpacing (% of line height, a block gap after each
//      paragraph, stacked on extraParagraphSpacing). It changes pagination, so it joins
//      the header's cache-key fields and forces a one-time rebuild.
// v36: firstLineIndent (space-widths) is replaced by firstLineIndentMode + firstLineIndentPercent
//      (Book vs Custom %). It changes first-line layout, so both join the cache-key fields.
// v37: the render spec gains guideDotsEnabled (draw a middle dot in a widened inter-word gap).
//      Widening the gap changes line breaks + page fill, and the on-disk TextBlock gains an
//      optional per-word guideDotXOffset array, so both force a one-time rebuild.
// v38: upstream's <ruby>/<rt> support (upstream numbered it v33; our numbering is
//      already ahead, so it lands here). Every on-disk TextBlock gains a per-word ruby
//      string, so the format changes on all books and forces a one-time rebuild.
// v39: paragraph numbering skips chapter headings and letterless blocks, so the stored
//      paragraphOrdinal values shift. Layout is untouched; the rebuild is for the numbers.
// v40: v39's heading skip never fired. The heading flag lived on the parser and was cleared
//      when the next element opened, which is the moment the heading block is laid out, so
//      every chapter title still took number 1. The flag now lives on the block. The stored
//      ordinals shift again; layout is untouched.
// v41: FOOTNOTE_HREF_LEN 96 -> 256 (upstream #2722). FootnoteEntry is written into
//      every page record by Page::serialize, so the on-disk record size changes.
// v42: <br> handling changed layout (upstream #2710; upstream numbered it v34). A <br>
//      after text is now a margin-stripped line break, browser-like, and only a <br>
//      whose block stays empty injects the scene-break gap. Cached pages laid out by
//      older versions no longer match. Keeps <br>-per-paragraph books (common CJK and
//      Korean web-novel formatting) from re-adding container spacing at every paragraph.
// v43: word gaps are only suppressed for tokens glued together in the source, so the
//      spaces between Hangul words survive again; ruby element boundaries now carry the
//      continuation flag themselves (upstream #2768; upstream numbered it v34). Caches
//      built by older versions have those spaces collapsed into the word positions.
// v44: every paragraph's first line now carries at least a three-space indent, in both
//      Book and Custom % mode and whatever Extra Paragraph Spacing says. First-line
//      widths move, so pagination differs from every earlier cache.
// v45: every page record gains a uint32 visible-text start offset, and the header gains a
//      fifth placeholder for the visible-offset LUT (upstream #2805; upstream numbered it
//      v35). Reading positions become content-based, so sync survives a re-layout and maps
//      onto other readers. The header and page records both grow, forcing a one-time rebuild.
// v46: simple HTML table rows are laid out as positioned columns instead of flattened
//      paragraphs with synthetic "Tab Row N, Cell M:" labels (upstream #2654; upstream
//      numbered it v36). Table cells occupy different line positions than before, so
//      cached pages built by older versions no longer match.
// v47: the anchor map stores a uint64 arxHash64 of each anchor instead of the anchor
//      string (upstream #2456). Keys become fixed-width, so a v46 map reads as garbage
//      and must be rebuilt. Pagination itself is unchanged; this is a RAM fix, cutting
//      up to ~32 KB of vector slots and up to MAX_ANCHORS_PER_CHAPTER separate heap
//      blocks out of a chapter parse, and allocating nothing per entry on lookup.
// v48: ruby annotations no longer shift the words under them at render time. Layout
//      reserves the annotation's overhang on both edges of the line instead, so a wide
//      annotation can never spill past the right margin when the line is justified, and
//      CJK lines stop breaking early to leave room for it (upstream #2781; upstream
//      numbered it v36). Line breaks move, so every cached page must be rebuilt.
// v49: an image's top margin is clamped so a full-viewport-height image cannot overflow
//      the page bottom (upstream #2959; upstream numbered it v39). Older caches can hold
//      placements that panels with no bottom inset refuse to draw.
// v50: a flush inside a <ruby> group no longer splits the group, so the annotated
//      word keeps its ruby text (upstream #3102; upstream numbered it v40). Word
//      positions move on ruby books, so cached pages must be rebuilt.
// v51: Focus Reading emphasis became a byte annotation on a whole word instead of a
//      separate suffix token, and a visible hyphen or dash inside a word is now a break
//      opportunity (upstream #2892; upstream numbered it v38). Both change where lines
//      break, so pages cached by older versions no longer match.
// v52: the embeddedStyle header byte split into embeddedTextStyle and embeddedLayoutStyle,
//      one byte each, so Embedded Style became two independent switches. The header grew
//      by one byte, so older files no longer parse.
// v53: a table row holding a colspan or rowspan cell now flows stacked at full width
//      instead of as grid columns, and cell text inherits text-align, weight, style and
//      decoration from the enclosing <table>/<tr>/<tbody> (upstream #2654 as merged;
//      upstream numbered it v41). Cells land in different places, so cached pages built by
//      older versions no longer match.
// Not a version: the Hidden Dots sub-option (keep the widened guide-dot gap, draw no
// dot) reuses the v37 guide-dots header byte as a three-state mode, so the header keeps
// its size and only books whose guide-dot state actually changed are rebuilt.
// v54: an internal EPUB link keeps its CSS vertical-align, so a footnote marker
//      written as <a style="vertical-align: super">1</a> renders raised again
//      (upstream #3355; upstream numbered it v45). The link branch returned before
//      applying the resolved style, so superscript markers sat on the baseline.
//      Marker glyphs change size and position, so cached pages must be rebuilt.
// 55: honour the HTML hidden attribute as display:none (upstream #3390). Hidden
//      nodes leave the page, so cached pages must be rebuilt.
// v56: balance full text pages vertically; cached y positions change, binary layout does not.
// v57: distribute every justification pixel; cached x positions change, binary layout does not.
// v58: closing a child block no longer reapplies the parent's vertical margins/padding (#3221).
// v59: inline direction changes no longer replace the paragraph's base direction (#3198).
// v60: font ligatures no longer collapse already-shaped Arabic presentation forms (#3294).
// v61: wordSpacing enters the header; baseline gaps change pagination.
// v62: remove the hyphenation header byte and automatic word splitting.
// v63: a soft-flushed long paragraph no longer re-indents its continuation lines.
// v64: a CSS-hidden <rt> clears its skip on close; the element after it no longer vanishes.
// v65: table rows use the rounding line-height overload; they were 1 px short.
// v66: a word holding Hangul (Hanja included) stays whole and wraps at spaces; justification no
//      longer stretches between syllables (upstream #3700; upstream numbered it v48). A Korean
//      word wider than the line splits between syllables instead. Line breaks and word positions move.
// v67: ordered lists number their items, list-style-type: none drops the marker, and <ul>/<ol>
//      add their own margins and padding to the items' inset (upstream #3500; upstream
//      numbered it v46). Marker text and list layout change.
// v68: a soft-flushed long paragraph no longer gets top margin/padding before its continuation
//      lines (upstream #3875; upstream numbered it v51).
// v69: the reader's Kerning switch enters the header; kerning off moves every line break.
// v70: the Ligatures switch enters the header; letters drawn one by one are wider.
// v71: the Link Underline switch enters the header; plain links are baked into the words' style.
// v72: the Book Margins switch enters the header; off drops the book's CSS margins and padding.
//      Embedded Layout Style off now drops them too (their values had leaked past the flags).
// v73: the Break Before Headings switch enters the header; on starts each h1/h2 on a new page.
// v74: Word Expansion enters the header, and each text line stores its letter spacing.
// v75: a line may break after a hyphen or dash inside a word ("foo—bar"; upstream #3883).
// v76: Character Spacing enters the header; tracking changes every word's width.
// v77: printed-page markers (<span epub:type="pagebreak" id=...>) enter the anchor map, so
//      Go to Page lands on the marker's page; spans were skipped before.
constexpr uint8_t SECTION_FILE_VERSION = 77;
// Written into the version field while a build is in progress; patched to
// SECTION_FILE_VERSION only when the build is finalized. An abandoned /
// crash-interrupted .bin therefore carries version 0, which loadSectionFile rejects
// as unknown and clears -- so an incomplete file is never mistaken for a valid one.
constexpr uint8_t SECTION_FILE_INCOMPLETE_VERSION = 0;
// Written when a build is suspended partway (reader exited or device slept mid-build).
// The file carries valid pages 0..pageCount-1, all LUTs, and a trailer with the parse
// watermark (bytesConsumed, totalBytes) appended after the li LUT. loadSectionFile
// accepts it so a resume shows those pages instantly; the reader extends it by
// rebuilding in the background. Uses the same header layout as SECTION_FILE_VERSION,
// so finalized files are untouched by this feature; older firmware treats the sentinel
// as an unknown version and rebuilds, which is a safe downgrade.
// MUST change in lockstep with SECTION_FILE_VERSION: the sentinel IS the partial's
// format version, so a stale-format partial otherwise passes the header check and
// only fails (noisily, via the block-decode error path) when a page is loaded.
// Derived so the pairing can't be forgotten: 0xFE for v28, 0xFD for v29, ...
constexpr uint8_t SECTION_FILE_PARTIAL_VERSION = 0xFE - (SECTION_FILE_VERSION - 28);
constexpr uint32_t HEADER_SIZE =
    sizeof(uint8_t) + sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(bool) + sizeof(uint8_t) + sizeof(uint8_t) +
    sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) + sizeof(bool) + sizeof(uint8_t) +
    sizeof(bool) + sizeof(bool) + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(bool) + sizeof(bool) + sizeof(bool) +
    sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t) +
    sizeof(uint8_t) + sizeof(uint8_t) + sizeof(int8_t);
// The header ends with the patched-later fields, in this order. Named once so a seek
// never re-derives its offset from the end of the header by hand.
constexpr uint32_t PAGE_LUT_FIELD = HEADER_SIZE - sizeof(uint32_t) * 5;
constexpr uint32_t ANCHOR_MAP_FIELD = HEADER_SIZE - sizeof(uint32_t) * 4;
constexpr uint32_t PARAGRAPH_LUT_FIELD = HEADER_SIZE - sizeof(uint32_t) * 3;
constexpr uint32_t LI_LUT_FIELD = HEADER_SIZE - sizeof(uint32_t) * 2;
constexpr uint32_t VISIBLE_LUT_FIELD = HEADER_SIZE - sizeof(uint32_t);
constexpr uint32_t PAGE_COUNT_FIELD = PAGE_LUT_FIELD - sizeof(uint16_t);
}  // namespace
