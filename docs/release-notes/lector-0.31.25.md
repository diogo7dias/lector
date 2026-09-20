# lector 0.31.25

Diagnostics only. No feature or behaviour changes to reading, syncing or the UI.

This release exists to find out why OTA fails on the X3 and the X4 while it
works on the X4 Pro. Nothing here fixes that bug: it makes the failure leave
evidence behind. The X4 Pro's own diagnostics file showed three clean OTA
installs in a row, which is what ruled out the download and flash machinery and
left the C3 devices as the thing to measure.

## Wi-Fi join evidence

The X3 sits on "Connecting..." forever and never reaches OTA at all, so the OTA
diagnostics could not see it. The join path now records its own progress:
session state and every transition, the Wi-Fi status codes actually observed and
how long each one persisted, whether the action queue is being drained, scan
results and whether the target network appeared, which timeout budget applies
and whether it fired, and free heap at the start of the join.

Bounded on purpose: at most a 6 KiB file, 48 immediate checkpoints and then one
record every five seconds, so a ten-minute stall writes at most 168 records and
cannot fill or wear the card.

No network name, password, password length, MAC or BSSID is ever written.
Networks are identified by index and signal strength only.

## OTA transfer evidence on the C3

For the X4's "update failed", the transfer path now records allocation failures
with their sizes, scratch low-water and spill counts, TLS out-of-memory details,
and the exact flash feed result with bytes written against capacity.

Compiled for the C3 only. The X4 Pro binary contains none of it.

## What to do

Update an X3 or an X4, let it fail as it normally does, then send
`/lector-flash-diagnostics.txt` from the top level of the SD card.
