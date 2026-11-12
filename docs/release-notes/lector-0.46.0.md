# Lector 0.46.0

## New

- **Auto Sync** (KOReader Sync settings, off by default) keeps your place in step with BookOrbit: it pulls the position when a book opens and pushes it when the device sleeps. It joins saved Wi-Fi networks only.
- **BookOrbit warning on Home**: when a book on your BookOrbit "Reading" shelf is not on Lector, Home shows "N on BookOrbit, not here". Open it to download the book; it opens at your BookOrbit position.
- **Reading Stats Position** for the sleep band: Center (default), Left or Right.

## Changed

- The built-in Kumedia OPDS server now points at BookOrbit; saved copies of the old URL update themselves.
- Firmware files install on CrossPoint and CrossInk's SD updaters again, and Lector no longer checks firmware signatures.
- Power works like CrossPoint 1.6.5: the CPU slows down when idle instead of light-sleeping. This fixes the X3 Loading freeze and a USB lock-up on the X4.
- Less static RAM, and the online update progress bar moves again.
