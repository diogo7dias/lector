# lector 0.31.26

Fixes OTA updates on the X3 and the X4.

## What was wrong

Both devices refused the update with "Not enough memory for a secure
connection". A device log from an X3 caught it exactly:

    tls heap gate step=check    free=22908 ... floor=22528 -> allowed
    tls heap gate step=install  free=22088 ... floor=22528 -> REFUSED

The update was admitted at the check, then refused 11 seconds later at the
install, 440 bytes under the floor.

The memory was not lost: it was never given back. The Settings screen stays
alive underneath the update screen, and it retained row capacity for EVERY
settings category, not only the one opened. `clear()` destroys the contents
of a vector but keeps its allocation, so roughly 7.5 KB sat reserved while
the update asked for the last 440 bytes it needed.

The font download path already did this correctly, releasing its rows before
its own gate. The update path never did. Same gate, same device, one path
tidied up and the other did not.

The X4 Pro has PSRAM and clears the same gate with around 120000 bytes free,
which is why it always updated and the other two never did.

## The fix

Retained UI capacity is released before the update asks to start its secure
connection. Measured by element size and capacity rather than estimated: at
least 7560 bytes, against the 440 needed, with room for the roughly 1900-byte
run-to-run variation in free memory at that moment.

The memory floors themselves are unchanged. Lowering them would have traded a
clear error message for the reader hanging until its watchdog restarts it.

The diagnostics file now records what was actually reclaimed, so a device log
shows the real number rather than this projection.

## Not changed

Download, flash, verification and rollback behaviour. The Wi-Fi join path: a
device log confirmed the X3 associates in 6.5 seconds with no disconnects,
so the earlier suspicion about Wi-Fi retries was wrong.
