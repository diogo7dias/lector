## Fixed
- **Another firmware flashed from the SD card now stays flashed.** Picking a stock or CrossInk `.bin`, from **Settings > System > SD Card Firmware Update** or from recovery mode, ran the whole progress bar and then came back to Lector on the next boot, over and over. The record that tells the bootloader which slot to start asked it to keep an eye on the new image and undo the switch unless that image reported itself healthy. Lector reports that automatically; other firmware does not, so it was quietly rolled back every time. The switch is now final. Everything that guarded the flash before still guards it: the file's header, chip id, segment table, checksum and SHA256 are all verified before a byte is written, and a bad file is refused before the boot record is touched.
- **Locking the reader no longer loses the last lines of the log.** The serial buffer was cut off mid-sentence on the way into sleep.
- **A missing `/sleep.pxc` is not an error.** Having no sleep image is a choice, and it is no longer reported as a failure.

## Changed
- **The sleep path is measured now.** Each stage of locking the device, saving state, painting the sleep screen, the Quick Resume frame, Wi-Fi teardown, powering down the panel, is timed and written beside the refreshes that paid for it, and the previous wake's split is logged too. Turn it on with **Settings > System > Performance timings**.
- **Wallpaper picking got a little quicker** on the way into sleep: the chosen file is looked up once instead of twice.
