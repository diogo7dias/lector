# Lector 0.40.0

## Added

- X4 Pro: **USB Drive** (File Transfer > USB Drive) opens the SD card on a computer over the USB cable.
- X4 Pro: **Touch Sleep**. The touch panel sleeps after 30 s idle and the buttons wake it.
- Frontlight **Max Brightness**, 10 to 100 (default 30).
- Paragraph numbers have a **Number Size**: 35%, 65% or 100% of the text.
- The in-book menu changes values with plus and minus.
- One slider look everywhere, one step per press. Lines are 2 px, armed rows get a dashed border on all four sides, and the light panel matches the menu.
- More X4-family panels are recognised (UC8279 variants and BOE 4.28 glass), and the X3 panel probe follows stock V6.3.15.
- The web page loads faster on repeat visits (ETag and Cache-Control).

## Fixed

- A selected Settings row no longer goes blank after a tap.
- Korean: a word wider than the line splits between syllables, and justified text stretches only at word spaces.
- The frontlight stays as it was across a silent restart.
- Reopened books no longer pin memory, and SD-font caches are released before chapter layout and on leaving the reader.
- Ordered lists are numbered and list containers indented.
- Progressive JPEGs with luma-only scans decode.
- Screenshot folder names are no longer cut mid-letter.
- Ported upstream fixes: images in chapter builds, paragraph spacing after a soft flush, large spines, SD-font kerning and ligatures, chunked WebDAV downloads.
- X4 Pro: the idle task stack is raised to IDF's 1536 B.
