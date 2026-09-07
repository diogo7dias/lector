## Changed
- **Unlock is one panel pass on every sleep face.** The fast wake path was written for the custom wallpaper face and gated on it. Crest (the default) went through a boot splash plus the activity painting over it. Deep-sleep unlock now blanks while the button is still held, one FULL pass, then the first paint. Quick Resume is unchanged — that face keeps its pixels.
- **Complexity cuts.** Dead icon headers, never-called functions, a one-theme layer that still carried a vtable, duplicated BMP writers, unused uzlib compressor, and the unused Lock Lab UI half are gone. Flash is a few kilobytes smaller; behaviour is not.

## Fixed
- **Large OPDS feeds no longer die mid-body.** Calibre-Web `/opds/discover` was 130 KB and wolfSSL returned MEMORY_E at 13 553 bytes: the handshake took the one framebuffer scratch slot, then the first 16 KB TLS record malloc'd into a 12 KB hole. The parser now claims a fixed arena before the transfer, and the framebuffer is split into two scratch slots so handshake and record both fit.
- **KOReader sync cannot lose local progress.** A failed rename no longer drops the stored position. Credentials are trimmed at the store. A server percentage the device cannot use is ignored rather than applied. The reader is told what a sync did, including when it failed.

## Notes
- This is the first tagged release that attaches `firmware-x4pro.bin`. An X4 Pro on 0.29.5 cannot OTA to it (that build does not ask for the S3 asset). First hop: SD `update.bin`, or one USB flash of this image. After that, Settings → Check for updates is enough.
- Version bump only, so devices already on 0.30.0 can OTA. Firmware content is identical to 0.30.0. An X4 Pro on 0.29.5 still cannot OTA this; it needs the 0.30.0 USB or SD hop first.
