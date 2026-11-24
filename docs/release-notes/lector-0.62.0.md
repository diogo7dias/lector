# Lector 0.62.0

## Fixed

- **Unlocking no longer stops to check the wallpapers again and again.** "Checking wallpapers" ran on many wakes and could take a long time on a big sleep folder. A wallpaper the reader cannot open now just gets skipped. A wallpaper deleted or paused on the device, the reader menu's delete included, also no longer sends the next unlock through a full folder scan. The scan still runs when the folder really changed, such as a file removed from a computer.
