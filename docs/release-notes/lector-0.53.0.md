# Lector 0.53.0

Every book re-indexes once when you first open it, because the layout cache changed. Reading positions are kept.

## Fixed

- **Stored passwords** can no longer be read over Wi-Fi through an alternate file name. Wi-Fi, OPDS and sync passwords and trusted certificates are never shown or served over the network, even with Show Hidden Files on.
- **Footnotes** open on the page where the note starts, instead of one page early.
- A note's return arrow now returns you properly, and Back no longer jumps to an old note after you move elsewhere in the book.
- A failed read can no longer produce a wrong book identity for position sync.
- A sync request is no longer sent twice when the server drops the first answer.
- The battery percent on X4 no longer drifts between screen draws.
- A chapter build parked behind the reading window no longer holds the processor at full speed.
- Idle Nearby screens follow Time to Sleep.

## Changed

- Long dictionary definitions turn with the same taps and side swipes as a book page, on touch screens.
- X4 Pro: opening chapters and images no longer rescans the book's file table.
- X3 and X4: about 2.7 KB more free memory and 125 KB less flash, by compiling out network features the firmware never uses.
- The status bar reads the chapter once per chapter instead of on every page.
