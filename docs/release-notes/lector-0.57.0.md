# Lector 0.57.0

No re-index this time: books open on their existing layout cache.

## New

- **Sharper paragraph numbers.** Paragraph numbers are now drawn as crisp pixel digits instead of a shrunk copy of the reading font, which turned grey and unreadable at the smallest size. Number Size is now **Small, Medium or Large** (8, 16 or 24 px tall); your saved choice carries over.
- **Number Position.** A new setting, in Reader settings and the in-book Look tab: **By the Text** (the new default) puts each number in the indent just before the paragraph's first letter, **Page Edge** keeps the column at the left edge. Set per book, and saved in reading themes.
- **Numbers centred on the line.** The numbers now sit centred on the capitals of the first line, so the larger sizes no longer hang below it.

## Fixed

- **Opening pages kept:** a book you left on its cover or front matter reopens there instead of jumping ahead to the start of the text.
- **Footnotes:** the layout cache no longer stores leftover memory bytes after each footnote's number and link.
