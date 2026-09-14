# Lector 0.31.6

A single fix: OTA on the X3.

## OTA over Wi-Fi on the X3

The X3 could not update over the air. Every attempt ended on "Low memory for
secure connection" before a byte was downloaded, while the X4 Pro updated every
time.

Two paths ask for a TLS connection during an update, and only one of them was
lending the display buffer to wolfSSL. The download did; the check for a new
release did not, so it went looking for ~30 KB of free heap that a C3 with Wi-Fi
up does not have, and refused before it started. The X4 Pro has 8 MB of PSRAM
and sailed past the same gate, which is why it never showed the fault.

The release check now takes the same framebuffer loan the download already had,
and the memory requirement is measured for each case instead of being one flat
number: 30000 bytes when the connection runs on the heap alone, 24000 when the
framebuffer is lent, and 8192 bytes of contiguous space either way. The lower
figure is what an observed transfer actually needed with the buffer lent, not a
threshold lowered until the message stopped appearing.

The font downloader was gated on its own copy of the same constant and now
shares the measured one.

If the reader ever does run out of memory here, the screen prints the two
numbers it measured rather than a sentence, so the failure can be read off the
device instead of guessed at.

## Upgrading

Settings > System > Check for updates, from 0.31.5. Nothing else changed;
skip this one if your reader already updates.
