# Lector

Lector is e-reader firmware. This glossary fixes the words used for books and reading position, on the device and on the server it syncs with.

## Language

### BookOrbit

**BookOrbit**:
The reader's self-hosted book server, holding the library and the reading position for each book.
_Avoid_: the server, kumedia, the library

**Reading shelf**:
The BookOrbit smart shelf named Reading, holding every book whose status is reading there. It is the only source of what counts as being read on BookOrbit.
_Avoid_: currently reading, in-progress list

**On the device**:
A book is on the device when the file a catalogue download would save exists, or a recent book has the same title and its file still exists. Name-based on purpose, unlike Position sync.
_Avoid_: downloaded, local, present

**Missing book**:
A book on the Reading shelf that is not on the device.
_Avoid_: unsynced book, absent book, a book whose file was deleted

**Read-along book**:
A book BookOrbit holds as text with matching narration. The device receives only the plain text copy, and its position still syncs.
_Avoid_: audiobook

### Sync

**Position sync**:
Exchanging a reading position with a server for one book, matched by the book file's contents.
_Avoid_: progress upload, KOSync (the protocol, not the act)

**Auto Sync**:
The single opt-in switch for all unattended sync work: Position sync when a book opens or the device sleeps, and the check for Missing books.
_Avoid_: background sync

### Moving through a book

**Link stack**:
The places Back can return to, one for each followed link the reader has not yet returned from.
_Avoid_: footnote depth, back history

**Deliberate jump**:
A move the reader chose that does not come from following a link: table of contents, Go to Page or Paragraph, a search result, or applying a synced position. It empties the Link stack.
_Avoid_: explicit jump, navigation

### File Transfer

**Protected file**:
A file on the card that is never served over the network, whatever the reader's settings: stored Wi-Fi, OPDS and sync credentials, and pinned server certificates.
_Avoid_: hidden file, sensitive path
