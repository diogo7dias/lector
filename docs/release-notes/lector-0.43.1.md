# Lector 0.43.1

## Fixed

- Check for Updates works again. GitHub moved to a certificate chain (P-384, SHA-384) that 0.41.0–0.43.0 could not verify, so every check failed with "Server check failed". Readers on those versions install 0.43.1 from the SD card (Settings > System > SD Card Firmware Update) or the browser flasher; updates over Wi-Fi work from then on.
