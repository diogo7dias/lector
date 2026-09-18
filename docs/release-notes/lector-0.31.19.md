# lector 0.31.19

## Fixes

- **Font downloads work again on the X3 and the X4.** The font list simply never
  arrived on these readers. Fetching it needs one large block of memory, and with
  Wi-Fi switched on there was never quite enough of it free, so the download
  failed every time rather than now and then.

  The reader now borrows the screen's own memory for the duration of the fetch,
  which is what the font *files* below the list already did. The same gap had
  already been found and patched twice before, in update checks and in the
  per-file download. This was the last place still missing it, and there is now a
  check that stops a fourth from being written.

  If the memory is still short, you get a message instead of a reader that sits
  frozen for a minute and then restarts itself.

## Faster

- **More free memory on the X3 and X4, everywhere the network is used.** The
  Wi-Fi driver was holding buffers sized for heavy multi-stream traffic, which a
  reader fetching one file at a time never needs. Trimming them hands back memory
  for the whole time Wi-Fi is on, which is exactly when these readers were
  tightest.

  This should help updates, catalogue browsing, Calibre, the web file manager and
  device-to-device transfers, not only fonts.

  The tradeoff: peak download speed on a weak signal may be slightly lower. If a
  download feels slower than it used to, say so and the buffers go back up.

- **Longer battery life between charges.** The processor now slows and naps on
  its own during the gaps while you read, instead of only in the places the
  firmware asked it to. Your page turns, taps and buttons should feel the same.

  There is a real figure to check rather than a claim: turn on Performance
  Timings and the device records how much of its time it actually spent asleep.
  Run it on battery, not on USB — a device on USB never sleeps.

  The tradeoff: this is the first release where the processor changes speed by
  itself. If page turns, taps or the screen look wrong in any way, say so and it
  goes back.

## Changed

- **An interrupted book transfer picks up where it stopped.** Sending a book over
  Wi-Fi or from another device used to throw away everything received the moment
  the transfer broke — a book that stopped at 90% cost the whole 90% again.

  Partial files are now kept and finished later. They never appear in your
  library until the whole book has arrived and been checked, so a half-finished
  file can't be opened by mistake.
