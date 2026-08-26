Lector test kit
===============

The quickest route is the one-line command that comes with each kit: paste it
into Terminal and it downloads this kit, flashes, records, and removes itself,
leaving only the log in Downloads. What follows is the manual route.

1. Plug the reader into the Mac with a data cable (a charge-only cable will not
   show up).
2. Double-click run.command.
   macOS may refuse the first time ("unidentified developer"). Right-click
   run.command, choose Open, then Open again on the dialog. This is needed once
   per kit.
3. It flashes the firmware, then keeps recording what the device says.
4. Run whatever checks were asked for on the reader itself.
5. Come back to the terminal window and press Return.
6. Send back the file it names, in your Downloads folder:
   lector-test-<version>-<date>-<time>.log

The log holds the firmware version, the commit it was built from, the flashing
output, and every line the device printed, including any crash dump.

Nothing here talks to the network, and nothing is uploaded. The log stays on
your Mac until you send it.
