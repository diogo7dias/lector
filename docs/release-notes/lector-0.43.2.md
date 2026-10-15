# Lector 0.43.2

## Fixed

- X3: leaving a WiFi screen (file transfer, KOReader sync, OPDS, Check for Updates) no longer leaves the reader stuck on "Loading" until Power is held. On battery, the restart after WiFi was switching the X3 off. Readers on 0.43.1 install 0.43.2 from the SD card (Settings > System > SD Card Firmware Update) or the browser flasher.
- If a restart ever does stall, the reader recovers by itself after 30 seconds and notes the step it reached in `/lector-flash-diagnostics.txt`.
