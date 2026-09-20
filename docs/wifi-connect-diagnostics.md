# Wi-Fi connection capture

This build extends `/lector-flash-diagnostics.txt` before OTA begins. No Wi-Fi
policy changed. Both C3 and S3 use the same Arduino APIs and fixed-size counters;
there is no PSRAM requirement, new task, timer, or instrumentation heap buffer.
Added fixed fields are under 256 bytes per Wi-Fi activity plus 24 bytes of static
station-event atomics; formatting reuses DiagLog's existing static buffers.
The existing writer still uses its temporary 6 KiB retention allocation. If that
allocation fails it keeps the current buffer only, with an explicit notice.

## Capture on an X3

1. Copy the **default** build's `firmware.bin` to the top level of the SD card.
2. Put the card back in the X3. Open **Settings > System > SD Card Firmware
   Update**, select that file, and confirm. Let the reader finish and restart.
3. Open **Settings > System > Check for updates**. Join Wi-Fi as usual (let a
   saved connection run automatically if that is how the hang occurs).
4. Leave **Connecting...** alone for **two minutes**, even if it looks frozen.
   If a failure or network list appears, leave that screen for ten more seconds.
5. Power off before removing the card. Send the whole
   **lector-flash-diagnostics.txt** from the top level of the card, beside the
   firmware file. Do not repeat the connection first: later captures replace
   older detail.

## Read the evidence

All `t`, `since`, `armed`, `expires`, `last`, and duration values are milliseconds
from boot, modulo 2^32. Timeout comparisons remain rollover-safe. Entry headers
also carry boot-session tags and uptime; the file header identifies the firmware
and board. Counters reset on entering a new Wi-Fi screen, except station event
counters, which run for the boot.

| Question | Evidence in the file |
| --- | --- |
| Is automatic retry cycling? | `state=AUTO_CONNECTING` with rising `joins`, `scans`, `fired`, and `auto_tried`; detailed actions identify DISCONNECT / SCAN / JOIN. `NETWORK_LIST` proves it surfaced the exhausted attempts. |
| Is the loop/drain stopped? | Compare `loop`, `nextAction/checkTimeouts`, last timestamps and maximum gaps across checkpoints. An overdue `timer` with an unchanged drain count means it has not been evaluated. If all entries stop, the final persisted `phase` identifies the last reached boundary; no timer/task is secretly evaluating timeouts. |
| Is status pinned? | `status` gives its numeric code and name, `since` and `dwell_ms` its observed duration. Rising loop, drain and poll counts with unchanged status distinguish a live loop from a blocked call. |

`seen_mask` preserves all observed status codes (bits 0–6, bit 30 for 254,
bit 31 for 255). `sampled_ms` preserves cumulative intervals assigned to each
previous sample, in that same order. These measure sampled observations, not
unobserved radio transitions between polls. A new join resets the current dwell
window so a scan or keyboard pause is not misreported as a continuously pinned
status. `WiFi.begin returned` is deliberately separate from `WiFi.status`.

The latest join's automatic/manual mode, saved-credential flag, heap, timeout,
scan count, target presence/index and strongest scanned RSSI remain in snapshots
after early detail expires. `target_seen=-1` means no scan/target was available,
not that the SSID was absent. Direct saved joins do not invent scan results.
The SDK chooses the actual BSSID: `strongest_rssi` is the scan candidate;
`associated_rssi` is the associated AP's measured RSSI (0 means unknown).
Station event counts and disconnect reasons are copied from the Arduino event
task using atomics; only the main task formats records or writes the card.
No SSID, password, password length, MAC, BSSID or IP enters these records.

## Bounds and limits

The RAM text ring is the existing 2 KiB; the entire on-card file is capped at
6 KiB including headers, even if the retention allocation fails. There are at
most 48 immediate writes per screen entry, then at most one every five seconds:
no more than 168 rewrites / 1,032,192 bytes of file payload over ten minutes
(filesystem metadata writes are card-dependent). Cumulative counters preserve
evidence while the oldest detailed transitions roll out. Buffer overflow is
reported. The writer still rewrites in place: interrupted SD writes can lose the
file, and an unavailable/broken card cannot provide evidence.

Checkpoints are outside render-lock scopes and never write from an ISR, event
callback or render task. `onExit()` only buffers a line because the activity
manager holds the render lock there.

Checkpoints add SD latency and can perturb timing. This is application-level
evidence, not a packet trace or a CPU backtrace.
A blocked call's last boundary does not identify the instruction inside the
Wi-Fi driver. After the immediate-write allowance is exhausted, the last
persisted boundary can precede the stall by up to five seconds; unwritten RAM
notes are lost on power-off. No associated-AP RSSI exists if association never
happens. If the entire processor or storage stops, this cannot independently
record the elapsed time after its last successful write. Hardware reproduction
is still required; host tests and both builds do not prove an X3 connection.
