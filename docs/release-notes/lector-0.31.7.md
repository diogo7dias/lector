# Lector 0.31.7

When an update fails, the reader now writes down what actually happened.

## Why

A reader hit a failed SD install and sent the diagnostics file. It said the
firmware image was zero bytes. It was not: the zero was a placeholder the
firmware printed before it had opened the file, and it sent the investigation
in the wrong direction while the real cause sat one line further down. The
same file had no way to tell two attempts apart, so a photo of the failure
screen could not be matched to any entry in it.

A diagnostics file that prints a stand-in as if it were a measurement is worse
than no diagnostics file.

## What changed

Every field is now either a real measured value or the word `unknown`. No
placeholder is ever printed as data.

The readback mismatch offset was already being computed and thrown away. It is
now recorded and shown: the byte where flash stopped matching the file, with a
note for whether it landed on a 4 KiB sector or 64 KiB block boundary. That
distinguishes a bad erase from a failing card from a brownout.

Battery voltage is recorded at the start of every flash attempt. A 4 MB write
on a low battery browns out the supply rail and produces exactly the mismatch
described above.

Each entry carries a session tag and an attempt number, and names the same
result the screen names, so a photograph of the failure and a line in the file
can be put side by side.

Coverage now extends past firmware flashing to over-the-air attempts including
the memory checks, abnormal boots (panic, watchdog, brownout), SD mount
failures, and whether the firmware that was installed is the one that came up
afterwards.

## Cost

Nothing is written to the SD card while reading. Entries are held in a 2 KB
buffer in memory and written only when an attempt ends, something fails, or the
diagnostics page is opened. A page turn writes nothing; waking now writes less
than it used to.

The file is a single file, capped, holding two days. Older entries drop out.
The newest entry survives whatever the clock does, since the reader has no
guaranteed sense of time.

## Privacy

No book titles, authors, library paths, reading positions, network names,
addresses, or server details are recorded, and there is no device identifier.
The session tag is random per boot. What it keeps is firmware version, board,
error codes and the stage they came from, memory, battery, and timings.

## Upgrading

Settings > System > Check for updates.
