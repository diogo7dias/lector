# lector 0.31.22

One change: the ebook menu now opens its sections on focus instead of listing
everything at once.

## The ebook menu collapses

The menu opens with every section closed and the cursor on the first header.
Moving to another header opens that section and closes the one you left, so only
one is ever open. Moving down from a header walks into its rows; the section
stays open while you are inside it. Back collapses the open section and returns
to its header, and Back again leaves the menu as before.

Headers are now landable and tappable. Until this release they were drawn as
labels and skipped by the cursor, which is the rule that had to invert: with
everything collapsed, headers are the only rows left to land on. A plus/minus
indicator sits inside the header band so the state is readable when nothing is
open, and the shrink-wrapped header band from 0.31.21 is unchanged.

The underlying list component in the SDK gained opt-in section support. It is
off by default, so every other list in the firmware draws exactly as it did
before. Collapse state is one index held in memory: it is never written to
settings and never reaches the web API.

## Scope

This is the ebook reader menu only.

The main Settings screen is a grid, not a list, and it deliberately drops its
section headings: 121 settings would be 61 grid rows, which is why it is already
split into a hub and four categories. Collapsing it would undo that. Button
Bindings could adopt the same behaviour later; it needs visible-index mapping and
section-aware Back in its own two files.

## Not verified on hardware

Host tests cover the navigation rules: entry state, one-open-at-a-time, focus
moving between headers, walking in and out of a section, Back collapsing, and
the degenerate lists (a heading with no rows, two headings in a row, a list
starting without a heading). 1433 tests pass, up from 1418.

What they do not cover is how it feels. Touch has host coverage only, the
plus/minus indicator has never been seen on a panel, and e-ink refresh behaviour
when a section expands is unmeasured. A supplementary SDK test suite is reported
to carry three failures that predate this change; that claim has not been
independently confirmed here.
