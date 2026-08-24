## Fixed
- **Font downloads complete.** A font file arrives over TLS from a server that sends 16 KB records, and wolfSSL needs a receive buffer that size for the whole session. With Wi-Fi up the download started with about 43000 bytes free and ran out a few kilobytes short, every time. It now borrows the framebuffer for the length of the transfer, the same way the chapter builder already does, so the buffer costs the heap nothing. The progress bar holds still while a file is in flight; that is the price of the file arriving at all.
- **The font list no longer eats 29 KB it never gives back.** Reading the manifest left the screen with 18608 bytes free, which is where the download died. It now has 41216.
- **A retry that carries bytes counts as progress.** A transfer that stopped part way used to spend its five attempts and give up even while it was moving forward, and a partial was thrown away whenever the server answered a resumed request with the whole file.
- **OPDS says when the server refused the login.** A feed that came back 401 reported "Failed to fetch feed", which reads as a connection problem. It now says "Wrong username or password". The built-in catalog entry ships with no credentials, so it needs yours before it will answer.

## Added
- **Share Wi-Fi and OPDS credentials with another reader.** **Settings > System > Share WiFi and OPDS** sends this device's saved networks and catalog logins to another Lector over the Nearby radio, so a second device does not have to be typed in from scratch. Receive it from **File Transfer > Nearby Reader**, as for a file. The bundle carries passwords in the clear over an unencrypted radio and is deleted at both ends as soon as it is done; both devices ask first.
- **"Pages to Paragraph" status bar item.** Shows `>P.0` on most pages, meaning the paragraph you are in ends before the next one starts. `>P.2` means it runs two more pages.
- **"Never" is back as a Refresh Frequency.** It turns off the page-counted refresh only. The reader still cleans the panel when enough ink has moved to need it, so a long session does not end up smeared.

## Changed
- **The Caret selection style lost its underline** and now sits beside the row's first line of text, so a row that wraps is marked where reading starts. **Brackets** are a little finer.
- **The status bar's progress bar reaches the edge of the screen** when it is not floating, instead of stopping short of the bezel.
