# lector 0.31.23

Five features, three upstream text fixes, and the unlock banners removed.

## Sortes

A shelf of books you have already finished, and the itch to pull one down and
read a page for no reason. `Sortes Vergilianae` was the Roman practice of
opening Virgil at random and reading whatever you landed on.

Set the home footer button to **Sortes** (Settings, the same option that picks
Resume or Reading Stats). Pressing it opens a random finished book at a random
page.

Nothing is remembered. Page turns, the page you stopped on, the fact that you
were there at all: none of it is written. The book's saved position is
untouched, it stays finished, it does not return to your recent books, and the
visit does not count toward reading statistics. Back or the device sleeping
ends the visit.

Quotes and bookmarks still work while you are inside, because those are things
you deliberately chose to keep.

Only books at 100% are eligible, in any folder. If none are, the button says so
rather than opening an unfinished book.

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

## Faster nearby sync

Syncing your place between two readers used to mean opening the sync screen on
both and choosing "Take theirs" or "Send mine". Now both devices simply keep
whichever position is further along, with no prompt.

When it is *your* reader that moves forward, the screen says so in capitals, so
a jump you did not expect is never silent.

**Nearby Sync** now sits on the home screen where File Transfer used to be. It
offers two things: open the radio to receive a book together with its position,
or send your position to another reader with its radio open. A book received
this way arrives already open at the right page.

**File Transfer** moved into Settings as a fifth category, next to Display,
Reader, Controls and System. It does exactly what it did before.

Older readers ignore the position that rides along with a transfer, so sending
to a device on an older firmware still works.

## Two-tap confirmation on touch (X4 Pro)

A stray brush against the screen could previously trigger a setting. On touch
devices every tap now takes two: the first outlines the row with a thin solid
border, the second runs it. Tapping a different row moves the outline instead
of acting, and only the outlined row responds to a second tap.

This covers rows, toggles and the on-screen button hints. The keyboard, page
and scroll controls, and Yes/No dialogs are exempt: they are either already
deliberate or already confirmations.

Devices without a touch screen are completely unchanged.

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

1584 host tests pass, covering the return ring (eviction, cancelled jumps, a
failed destination keeping its origin), word spacing (measurement and
positioning agreeing at every value, flush justification at every value, NBSP,
CJK, Guide Dots) and Sortes (an unfinished book is never selected, the empty
shelf case, and a visit leaving saved progress byte-identical).

None of it has run on a panel. Unverified: whether the spacing increments are
useful in practice, how Return behaves across a real cache rebuild, how Sortes
feels against a real shelf on a real card, and heap headroom during repeated
jump and return cycles.

Nearby sync needs two physical devices to verify: radio retries, that both ends
converge on the further position, that the capitals warning is visible, and that
a received book opens at the right page across different fonts and orientations.

Two-tap needs a real finger on a real panel: whether the outline is visible
enough on e-ink, whether the second tap feels responsive rather than laggy, and
whether any screen still acts on a single tap when it should not.
