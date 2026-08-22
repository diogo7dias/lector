## Added
- **Settings is one long list.** Display, Reader, Controls and System run together in that order, headings kept. Stepping walks the rows as before; holding a step button now jumps to the first row of the next or previous section, which is what the tab strip used to be for. Holding backward is sticky: it snaps to the current section's first row before moving up. Back leaves Settings from any row.
- **The in-book menu can open on its Sleep Screen tab,** a fifth choice in Book Menu Opens On. When the lock screen has no wallpaper on the card the tab does not exist, and the menu opens on Navigate instead.

## Fixed
- **A firmware update survives a dropped connection.** One drop anywhere in the 5 MB image used to fail the whole update. It now makes up to three attempts and each one carries on from where the last stopped, costing the remaining bytes rather than all of them.
- **The reader no longer offers to forget a network every time a connection fails.** A connection drops for reasons that have nothing to do with the password. Forget is still there, on Left on a saved network in the list.
- **Connecting is steadier, worst on the X4.** The reader was erasing the stored network and powering the radio down before every attempt, which makes some routers fail the handshake that follows.
- **Downloads hold the radio at full power for their duration,** instead of letting it park between beacons and lose packets a weak link then has to resend. Font downloads, the update check, OPDS and sync all get this; only firmware installs had it before.
- **A section jump opens the section at its heading,** instead of leaving the arrived-at row against the top or bottom edge with its heading off screen.
- **A failed Wi-Fi connection says why in the logs,** naming the router's own reason rather than reporting a bare timeout.
