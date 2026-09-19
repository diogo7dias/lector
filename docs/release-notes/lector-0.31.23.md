# lector 0.31.23

Two reader features and three upstream text fixes.

## Return to where you were

Jumping to a chapter, dragging the progress bar, or opening a bookmark used to
replace your reading position with no way back. The reader menu now offers
**Return**, which takes you to where you were before the jump.

It records only deliberate jumps, never ordinary page turns, and keeps the last
eight. A cancelled chapter picker records nothing. The action is hidden when
there is nothing to return to. History lives in memory for the current reading
session: it is not written to the card and does not survive closing the book.

Positions are stored as content offsets, so a return still lands on the right
passage after a font or orientation change repaginates the book. Footnote return
behaviour is unchanged, and the Back button still does what it did.

## Word spacing

A new **Word Spacing %** setting in the reader, 75 to 150 in steps of 5, sets
the baseline gap between words. Justification still stretches from that baseline
and justified lines still finish flush at the right margin — this widens or
tightens every line, it does not cap how far a sparse line may stretch.

The default is 100, which is byte-for-byte the previous layout.

Guide Dots keeps working: both spaces around a dot scale with the setting, the
dot itself does not.

## Upstream text fixes

Ported from upstream crosspoint-reader:

- Paragraph spacing no longer misbehaves when extra paragraph spacing is off
  (#3221)
- An inline direction change no longer replaces a paragraph's base direction,
  fixing mixed right-to-left text (#3198)
- Font ligatures no longer re-collapse already-shaped Arabic and Persian
  presentation forms (#3294)

Two more were already present in this fork, and two were skipped because they
depend on an upstream reader refactor we do not carry: touch navigation for
EPUB links, and reader long-press shortcuts.

## Books re-paginate once

The section cache version moved 60 → 61 because word spacing changes where lines
break. Every book repaginates the first time you open it after updating. This
happens once per book and is expected.

## Not verified on hardware

1549 host tests pass, covering the return ring (eviction, cancelled jumps, a
failed destination keeping its origin) and word spacing (measurement and
positioning agreeing at every value, flush justification at every value, NBSP,
CJK, Guide Dots).

None of it has run on a panel. Unverified: whether the spacing increments are
useful in practice, how Return behaves across a real cache rebuild, and heap
headroom during repeated jump and return cycles.
