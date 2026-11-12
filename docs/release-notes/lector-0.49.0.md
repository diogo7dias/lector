# Lector 0.49.0

Every book re-indexes once when you first open it, because the layout cache changed. Reading positions are kept.

## Changed

- Pictures inside a book now use the same gray levels as covers, so a picture looks the same in both places. Each picture decodes once more on first view.
- Smaller firmware: an unused built-in font is gone, about 54 KB on each board.

## Fixed

- Word Expansion and Character Spacing count letters the way they are drawn: small caps no longer treat ligature pairs as one letter, and right-to-left words are counted as shown.
- An underline under the first word of an indented paragraph starts at the first letter when Word Expansion or Character Spacing is on.
- Markdown (`.md`) books get a sleep cover and show their title without ".md".
- Brightness set from the web settings page is kept in range, and a failed save reports an error instead of success.
- An XTC cover and a book's layout cache are written whole or not at all, so a power loss cannot leave a half-written file that is trusted later.
- Nearby position sync recovers progress left in a temporary file by an interrupted save.
