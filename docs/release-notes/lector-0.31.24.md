# lector 0.31.24

Receiving a reading position no longer needs the book open.

## Sync a book you already have

Until now, taking a position from another reader meant opening that book first.
Worse, if the sender sent the book itself, a copy you already had arrived as
"Book (2).epub" and the position landed on the duplicate rather than the one you
were actually reading.

**Home, Nearby Sync, Receive position** now waits with the radio open and no
book open. The sender shares from inside the book as before. The receiver takes
the book's identity off the air, finds that same book on its own card, and saves
the position into it. Open the book afterwards and you are where the other
reader was.

The book is matched by content identity, the same one KOReader sync uses, not by
filename alone. A position is never written to a book that did not match.

Only books you have already opened are candidates, since only those have a
reading position worth replacing. The screen names the file it matched, and says
plainly when it matched nothing.

Where a position cannot be mapped to your copy with certainty, it is refused
rather than guessed. A refusal leaves your existing position untouched.

## Unchanged

The over-the-air format is untouched, so syncing with CrossInk devices and with
readers on older firmware keeps working. Sharing from inside a book behaves
exactly as it did. Received files still never overwrite a file already on the
card.

## Not verified on hardware

1600 host tests pass, including the hash match, a mismatch, an empty card, and a
malformed position payload.

Two physical devices are needed to confirm the rest: that the radio finds its
peer reliably, how long the card scan takes with a full library, and that a book
opened after a received position really lands on the right page.
