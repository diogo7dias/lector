# lector 0.31.21

Three visible changes: justified text that actually reaches the right margin,
section headers that hug their label, and a fix for the ghosting left behind by
a wallpaper lock screen on the X3.

## Justification now uses every spare pixel

Justified lines shared out spare space with an integer division and threw away
the remainder, so a line with 20 spare pixels across seven gaps distributed only
14 and left 6 unused. The right margin drifted by a few pixels from line to
line, which is what made justified text look ragged on the right even though it
was justified.

Spare pixels are now handed out one at a time in positioning order, so the same
line distributes 3, 3, 3, 3, 3, 3, 2 and ends flush. Non-breaking spaces, CJK
gaps, ruby reservations and last-line behaviour are unchanged, and word spacing
is still uncapped.

The section cache version was bumped, so books re-paginate the first time you
open them after updating. That is expected and happens once per book.

## Section headers hug their label

Inverted section headers filled the whole row width. They now measure the label
and fill only that, left-aligned. `applyInvertedSectionHeaderStyle` is shared,
so every settings list in the firmware picks this up.

The title bar keeps its full-width band on purpose: that band is also what
clears the previous battery reading, and filling only part of it would leave
fragments of the old percentage behind.

## X3: the panel is scrubbed before a wallpaper lock screen

On the X3 the sleep path skipped its pre-clear, on the assumption that the
grayscale base pass already cleared the panel. It does not. That pass forces a
white baseline and paints against it, which drives only the transitions that
end white or start white; the black pixel's white excursion never runs. On a
grayscale wallpaper that leaves mid-tone pixels holding their previous charge,
so the reader page underneath survived into the lock screen as ghost text.

The scrub now runs on every board, with the pass count chosen per device. The
X3 needs a single white pass because its own waveform bank carries the black
excursion inside it; other boards keep the four passes they had.

### Not verified on hardware

The X3 analysis is derived from the waveform tables in the driver, not measured
on a panel. It predicts the ghost disappears, and it adds one panel update at
lock, which may be visible as a brief extra flash. Explicit Cover mode still
bypasses the scrub; cover-as-fallback gets it.

The TLS memory floors shipped in 0.31.20 remain as they were.
