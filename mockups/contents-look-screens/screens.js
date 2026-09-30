// Every screen, drawn from the inventories (what each screen shows today), in the contents look.
// S(title, source, note, body): body is the inner HTML of one 480 x 800 screen.
const S = (title, src, note, body) => ({ title, src, note, body });
const R = K.row, R2 = K.row2, L = K.list;
const PAGE = K.page(PROSE);
// Status bar in the reader's default layout, in Literata: chapter top left, counters below.
const SB = `<div class="sbt">chapter xix</div>`
  + `<div class="sbb"><span>3/40</span><span>+12<i></i>&gt;P.0</span><span>C:60%<i></i>Ch 2/12</span></div><div class="cbar"><i style="width:60%"></i></div>`;
const reading = (extra = '') => `<div style="padding-top:44px">${PAGE}</div>${SB}${extra}`;
const cent = (lines, top = 300) => `<div class="cent" style="top:${top}px">${lines.map((l, i) => `<div class="${i ? 'it' : 'lead'}">${esc(l)}</div>`).join('')}</div>`;

const GROUPS = [
  { name: 'Home and library', screens: [
    S('Home', 'HomeActivity', 'Top row (version, clock, battery) becomes the italic line and the folio. Recent books keep the author initials as the italic sub-line.',
      K.tp({ t: 'Lector', a: 'lector 0.35.0' })
      + K.head(1, 'Continue reading')
      + R2('The Left Hand of Darkness', 'UKLG', '42%', true) + R2('Middlemarch', 'GE', '23%') + R2('Dune', 'FH', '8%')
      + K.head(2, 'Library') + L([['Browse Files', '>'], ['OPDS Browser', '>'], ['Nearby Sync', '>'], ['Settings', '>']])
      + K.folio()),
    S('File browser', 'FileBrowserActivity', 'The path bar becomes the italic line. Folders open (›), books show progress or read. [brackets] go.',
      K.tp({ t: 'SD card', a: '/Fantasy' })
      + L([['Search this folder', '>'], ['Earthsea', '>'], ['Discworld', '>'], ['Dune', '42%', true], ['Hyperion', 'read'], ['Lord of Light', 'epub'], ['The Name of the Wind', '8%'], ['Tigana', 'epub']])
      + K.folio()),
    S('File actions', 'FileBrowserActivity (long press)', 'The "File" popup: a card with a heading, same rows.',
      K.tp({ t: 'SD card', a: '/Fantasy' }) + L(['Dune', 'Hyperion'])
      + K.card(K.head(0, 'Dune.epub') + L([['Send to Nearby Reader', null, true], ['Move to folder', '>'], 'Delete']), 300)),
    S('Delete confirmation', 'ConfirmationActivity', 'The question is the title, the file the italic line; Cancel is first so a stray press is safe.',
      K.tp({ t: 'Delete?', a: 'Dune.epub' }) + L([['Cancel', null, true], 'Confirm'])),
    S('Light panel', 'LightPanel (pull down)', 'Top panel over a stippled page. Toggles and levels as rows; the six actions as a two-column list; the readout as the folio.',
      `<div class="stip"></div><div class="panel">${L(['#Light', ['Frontlight', 'on', true], ['Brightness', '60'], ['Sort', 'alphabetical'], '#Actions'])}`
      + `<div class="g2">${['Refresh Screen', 'Rotate', 'Continue Reading', 'Random Book', 'Search', 'Settings'].map((x) => `<div>${x}</div>`).join('')}</div>`
      + `<div class="pfolio">battery 87% · 7.4 gb free</div></div>`),
  ] },
  { name: 'Dialogs and system', screens: [
    S('Number dialog', 'IntervalSelectionActivity', 'The value set large in the body face, a rule for the slider, the step sizes as one italic line.',
      K.tp({ t: 'Time to Sleep' }) + `<div class="big">10 min</div>` + K.rule(30)
      + `<div class="tips">front buttons: 1 min · side buttons: 5 min</div>`),
    S('Keyboard', 'KeyboardEntryActivity', 'Keys are text on hairlines. The cursor key is bold and underlined, no black fill.',
      K.tp({ t: 'Device Name' }) + `<div class="field">Lector-Dio<span class="cur"></span></div>`
      + `<div class="key" style="grid-template-columns:repeat(10,1fr);margin-top:26px">${'1234567890qwertyuiopasdfghjkl'.split('').map((c, i) => `<div class="${c === 'g' ? 'on' : ''}">${c}</div>`).join('')}<div>'</div>${'zxcvbnm'.split('').map((c) => `<div>${c}</div>`).join('')}<div>-</div><div class="w2">del</div>`
      + `<div class="w2">shift</div><div class="w2">?123</div><div class="w4" style="grid-column:span 4">space</div><div class="w2">ok</div></div>`
      + `<div class="tips">hold up to edit the entry</div>`),
    S('Keyboard, URL layer', 'KeyboardEntryActivity (URL)', 'Snippet keys share the grid; same hairlines, same cursor.',
      K.tp({ t: 'OPDS Server URL' }) + `<div class="field">https://books.example.org/opds<span class="cur"></span></div>`
      + `<div class="key" style="grid-template-columns:repeat(4,1fr);margin-top:26px">${['https://', 'http://', 'www.', '.com', '.org', '.net', '/opds', ':8080', '.local', '/', '-', '_'].map((c, i) => `<div class="${i === 6 ? 'on' : ''}">${c}</div>`).join('')}<div class="w2">abc</div><div class="w2">ok</div></div>`),
    S('Popup', 'OptionPopup', 'Every picker: card, heading, rows, current choice marked.',
      K.tp({ t: 'Display', a: 'settings' }) + L([['Refresh Frequency', '15 pages', true], ['Sunlight Fading Fix', 'off']])
      + K.card(K.head(0, 'Refresh Frequency') + L([['1 page'], ['5 pages'], ['10 pages'], ['15 pages', 'current', true], ['30 pages'], ['Never']]), 200)),
    S('Low battery', 'LowBattery popup', 'A small card; one row.',
      reading() + K.card(`<div class="cardt">Battery low</div><div class="tips" style="margin-top:0">9% left</div>` + R('Done', null, true), 300)),
    S('Toast', 'GUI.drawPopup', 'Paper strip with an italic line over a hairline, instead of the black band. Optional progress rule.',
      reading(K.toast('Going to sleep'))),
    S('Boot', 'BootActivity', 'The title page alone.',
      `<div style="padding-top:320px">${K.tp({ t: 'Lector', a: 'loading' })}</div>`),
    S('Sleep, wallpaper', 'SleepActivity', 'Badges become small paper labels in italic.',
      K.wallpaper() + `<div class="badge" style="left:18px">★ sunset.pxc</div><div class="badge" style="right:18px">12 / 348</div>`),
    S('Sleep, no wallpaper', 'SleepActivity (fallback)', 'Same as boot, without the loading line.',
      `<div style="padding-top:330px">${K.tp({ t: 'Lector' })}</div>`),
    S('Image viewer', 'BmpViewer / PxcViewer', 'Full-bleed image; the triage hint is one italic line on a paper strip.',
      K.wallpaper() + `<div class="toast" style="top:auto;bottom:0;border-bottom:0;border-top:1px solid var(--ink)">back · favourite · delete</div>`),
    S('Message', 'FullScreenMessageActivity', 'A title page on an empty sheet.',
      `<div style="padding-top:320px">${K.tp({ t: 'SD card error', a: 'insert the card and restart' })}</div>`),
    S('Crash', 'CrashReportActivity', 'The report stays literal; set in the body face at 16.',
      K.tp({ t: 'System crash', a: 'the reader restarted' })
      + `<div class="mono">Guru Meditation Error: Core 0 panic'ed (Load access fault)\nPC 0x42012a3c  RA 0x42012a10\nEpubReaderActivity::render\nheap 24244 / max 12788\n\nSaved to /crash/2026-09-30-1744.txt</div>` + K.hint('any button restarts')),
  ] },
  { name: 'Reader', screens: [
    S('Reading page', 'EpubReaderActivity', 'Only the status bar changes: Literata 19, chapter in small caps, counters in italic, hairline separators, same six anchors and bars.',
      reading()),
    S('Reading aids', 'EpubReaderActivity', 'Paragraph numbers in the 16 numeral face; a saved quote keeps its underline.',
      `<div style="padding-top:44px"><div class="page"><p><span class="pn">7</span>${PROSE[0]}</p><p><span class="pn">8</span><u>A young man stood near, looking at her.</u> He was a tall, slight man, with an air of careless ease.</p><p><span class="pn">9</span>${PROSE[2]}</p></div></div>${SB}`),
    S('In-book menu', 'EpubReaderMenuActivity', 'Built (exp.61). One flat list, numbered headings, no accordion.',
      K.tp({ t: 'Middlemarch', a: 'George Eliot', c: 'book ii, chapter xix', p: 'page 12 of 31 · 23% of the book' })
      + L(['#Navigate', ['Select Chapter', '>', true], ['Go to %', '>'], ['Go to Paragraph', '>'], ['Bookmarks', '>'], 'Toggle Bookmark', ['Footnotes', '>'], '#Book', 'Look Up', ['Lookup History', '>'], 'Grab Quote'])
      + K.more('down')),
    S('Contents', 'EpubReaderChapterSelectionActivity', 'Top-level TOC entries become headings, nested entries rows. Deeper levels indent 20px each.',
      K.tp({ t: 'Contents', a: 'Middlemarch' })
      + L(['#Miss Brooke', 'Chapter I', 'Chapter II', 'Chapter III', '#Old and Young', 'Chapter XIII', ['Chapter XIX', null, true], 'Chapter XX'])
      + K.more('down')),
    S('Bookmarks', 'EpubReaderBookmarksActivity', 'The snippet is the row, the place the italic line. The footnote replaces the hint strip.',
      K.tp({ t: 'Bookmarks', a: 'Middlemarch' })
      + R2('When they were at the Vatican, Dorothea let her…', '23% · 3/18 · chapter xix', null, true)
      + R2('Mr. Casaubon, as might be expected, spent a great…', '31% · 7/22 · chapter xxiv')
      + R2('Her full nature, like that river of which Cyrus…', '98% · 12/12 · finale')
      + K.fn('hold open to delete')),
    S('Footnotes', 'EpubReaderFootnotesActivity', 'Plain rows; the link text is the label.',
      K.tp({ t: 'Footnotes', a: 'on this page' }) + L([['12', '>', true], ['13', '>'], ['*', '>']])),
    S('Go to %', 'EpubReaderPercentSelectionActivity', 'Same shape as every number dialog.',
      K.tp({ t: 'Go to %', a: 'Middlemarch' }) + `<div class="big">42%</div>` + K.rule(42)
      + `<div class="tips">front buttons: 1% · side buttons: 10%</div>`),
    S('Look up a word', 'DictionaryWordSelectActivity', 'The chosen word is bold and underlined instead of a black box.',
      `<div style="padding-top:44px"><div class="page"><p>When they were at the Vatican, Dorothea let her husband pass on to the museum of inscriptions. She herself had gone through the long gallery of statues, and now stood by the reclining Ariadne, then called the Cleopatra, in the marble <b class="wsel">voluptuousness</b> of her beauty.</p><p>${PROSE[1]}</p></div></div>` + K.hint('look up · left · right')),
    S('Definition', 'DictionaryDefinitionActivity', 'The headword is the title; the page counter the italic line.',
      K.tp({ t: 'voluptuous', a: '1 / 2' })
      + `<div class="page" style="text-indent:0"><p style="text-indent:0"><i>adj.</i> 1. Of, relating to, or characterized by luxury or sensual pleasure. 2. Full and appealing in form. 3. Given to or spent in enjoyment of luxury or pleasure.</p><p style="text-indent:0;margin-top:10px"><i>From Latin</i> voluptuosus, <i>from</i> voluptas, pleasure.</p></div>`),
    S('Lookup history', 'DictionaryHistoryActivity', 'Words as rows; the destructive row sits last, after a heading of its own.',
      K.tp({ t: 'Lookup history' }) + L([['voluptuousness', '>', true], ['ardour', '>'], ['phantasmagoria', '>'], ['ecclesiastical', '>'], 'Clear History'])),
    S('Grab a quote', 'QuoteSelectActivity', 'Range as a thick underline, start word bold. Adds the one italic line the screen lacks today (which end is being picked).',
      `<div style="padding-top:44px"><div class="page"><p>${PROSE[0]}</p><p><b><u class="thick">A</u></b><u class="thick"> young man stood near, looking at her. He was a tall,</u> slight man, with an air of careless ease.</p></div></div>` + K.hint('pick the last word')),
    S('Quotes', 'QuotesViewerActivity', 'Quotes wrap in the body face; the chapter is the leader value.',
      K.tp({ t: 'Quotes', a: 'Middlemarch · 3' })
      + `<div class="q sel">Her full nature, like that river of which Cyrus broke the strength, spent itself in channels which had no great name on the earth.<span>finale</span></div>`
      + `<div class="q">What do we live for, if it is not to make life less difficult to each other?<span>ch. lxxii</span></div>`
      + K.fn('hold to delete')),
    S('Page as QR', 'QrDisplayActivity', 'Title page plus the code.',
      K.tp({ t: 'Page as QR', a: 'scan to read on a phone' }) + `<div style="margin-top:40px">${K.qr(340, 3)}</div>`),
    S('End of book', 'EndOfBookOptions', '"End of book" is the title; "Continue with" the italic line.',
      `<div style="padding-top:120px">${K.tp({ t: 'End of book', a: 'continue with' })}</div>`
      + L([['The Two Towers', '>', true], ['The Return of the King', '>'], ['Home', '>']])),
    S('Steal look', 'StealLookActivity', 'Recent books with their own look.',
      K.tp({ t: 'Steal look', a: 'copy another book\'s settings' }) + L([['Dune', null, true], 'Hyperion', 'Emma'])),
    S('Reading themes', 'ReaderPresetsActivity', 'Current theme marked; save sits apart.',
      K.tp({ t: 'Reading themes', a: '3 of 8 slots' }) + L([['Night Serif', 'current', true], ['Large Print', '>'], ['My Theme 2', '>'], 'Save current look'])),
    S('Theme actions', 'ReaderPresetsActivity (popup)', '',
      K.tp({ t: 'Reading themes', a: '3 of 8 slots' }) + L([['Night Serif', 'current'], ['Large Print', '>']])
      + K.card(K.head(0, 'Large Print') + L([['Apply', null, true], 'Rename', 'Overwrite with current', 'Delete']), 290)),
    S('Quick menu', 'Quick Menu popup', 'Unavailable rows are dimmed italic, not [X].',
      reading() + K.card(K.head(0, 'Quick menu') + L([['Bookmark', null, true], 'Dictionary', 'Grab Quote']) + `<div class="r off"><span class="l">Footnotes</span></div>` + L(['Status Bar']), 250)),
    S('Indexing', 'EpubReaderActivity', 'The toast with a progress rule over the previous page.',
      reading(K.toast('Indexing', 35))),
    S('Text book menu', 'TxtReaderActivity (Book Menu)', 'The .txt reader\'s own popup.',
      `<div style="padding-top:44px">${PAGE}</div>` + K.card(K.head(0, 'Book menu') + L([['Font', 'built-in', true], ['Size', '14'], 'Delete Book']), 290)),
  ] },
  { name: 'Settings', screens: [
    S('Settings', 'SettingsActivity (hub)', 'Five rows; counts as values. The version moves up into the italic line.',
      K.tp({ t: 'Settings', a: 'lector 0.35.0' }) + L([['Display', '11 settings', true], ['Reader', '11 settings'], ['Controls', '8 settings'], ['System', '20 settings'], ['File Transfer', '>']]) + K.folio()),
    S('Display', 'SettingsActivity › Display', 'The group headings the code already builds (and drops today) are drawn.',
      K.tp({ t: 'Display' })
      + L(['#Screen', ['Refresh Frequency', '15 pages', true], ['Sunlight Fading Fix', 'off'], '#Sleep screen', ['Sleep Screen', 'custom'], '#Wallpaper', ['Cover Mode', 'fit'], ['Cover Filter', 'none'], ['Show Wallpaper Name', 'off'], ['Show Favorite Badge', 'off'], ['Shuffle Wallpapers'], '#Home', ['Author On Home', 'initials'], ['Fast Unlock', 'on']])),
    S('Reader', 'SettingsActivity › Reader', '',
      K.tp({ t: 'Reader' })
      + L(['#Text', ['Text Settings', '>', true], ['Manage Fonts', '>'], ['Installed Fonts', '>'], '#Page', ['Reading Orientation', 'portrait'], ['Book Menu Opens On', 'sleep'], ['Paragraph Numbers', 'per chapter'], ['Number Size', 'small'], '#Look', ['Paperback Look', 'on'], ['Paperback Status Bar', 'off'], ['Customise Status Bar', '>'], ['Night mode', 'off']])),
    S('Controls', 'SettingsActivity › Controls', 'Long labels shorten only if they collide with the value; the leader shrinks first.',
      K.tp({ t: 'Controls' })
      + L(['#Back button', ['Short Back to File Browser', 'off', true], ['Home Back Button', 'resume'], '#Buttons', ['Buttons', '>'], ['Remap Front Buttons', '>'], ['Side Button Layout', 'prev/next'], ['Orient front buttons', 'off'], '#Hold and long press', ['Long press while reading', 'disabled'], ['Hold in in-book menu', 'disabled']])),
    S('System', 'SettingsActivity › System', '20 rows scroll; the arrow marks more below.',
      K.tp({ t: 'System' })
      + L(['#Power', ['Time to Sleep', '10 min', true], '#Library', ['Show Hidden Files', 'off'], ['File Browser Order', 'alphabetical'], ['Open Book on Boot', 'last book'], ['Clear Read Books', 'on'], ['Move Finished Books', 'on'], ['Move Opened Books', 'on'], '#Network', ['Device Name', 'lector-4f2a'], ['Wi-Fi Networks', '>'], ['KOReader Sync', '>']])
      + K.more('down')),
    S('Text settings', 'TextSettingsActivity', 'The live preview becomes a framed page sample; the cells a list below it.',
      K.tp({ t: 'Text settings' })
      + `<div class="pv"><div class="page" style="padding:0"><p style="text-indent:0;text-align:center;font-weight:700">Chapter One</p><p>Now these ashes have grown cold, we open the old book. These oil-stained pages recount the tales of the Fallen, a frayed empire, words without warmth.</p></div></div>`
      + L([['Font', 'chareink', true], ['Size', '14 pt'], ['Paperback Look', 'on'], ['Line Spacing', '95'], ['Alignment', 'justify'], ['Margin', '17']]) + K.more('down')),
    S('Status bar', 'StatusBarSettingsActivity', 'Anchors read as words (top left) instead of [TL].',
      K.tp({ t: 'Status bar' })
      + L([['Status Bar', 'on', true], ['Battery', 'off'], ['Title', 'top left'], ['Title Source', 'chapter'], ['Truncate Title', 'off'], ['Page in Chapter', 'bottom left'], ['Page Format', 'n/m'], ['Book %', 'off'], ['Chapter %', 'bottom right'], ['Chapter Number', 'bottom right'], ['Pages This Session', 'bottom centre'], ['Chapter Bar', 'bottom'], ['Bar Thickness', 'medium']]) + K.more('down')),
    S('Buttons', 'ButtonBindingsActivity (level 2)', 'In Book / Outside Book are headings.',
      K.tp({ t: 'Left button' })
      + L(['#In book', ['Single Click', 'previous page', true], ['Double Click', 'disabled'], ['Hold', 'disabled'], '#Outside book', ['Single Click', 'previous page'], ['Double Click', 'disabled'], ['Hold', 'disabled']])),
    S('Remap buttons', 'ButtonRemapActivity', 'The subheader is the italic line; the two side-button instructions the footnote.',
      K.tp({ t: 'Remap buttons', a: 'press a front button for each role' })
      + L([['Back', '1st button'], ['Confirm', 'unassigned', true], ['Left', 'unassigned'], ['Right', 'unassigned']])
      + `<div class="fn">side up: reset to default<br>side down: cancel</div>`),
    S('Pop-up items', 'PopupItemsActivity', 'Ticks become on/off values; the count is the italic line.',
      K.tp({ t: 'Pop-up items', a: '3 of 32' })
      + L([['KOSync', 'off'], ['Bookmark', 'on', true], ['Bookmarks', 'off'], ['Dictionary', 'on'], ['Grab Quote', 'on'], ['View Quotes', 'off'], ['Go to Paragraph', 'off'], ['Footnotes', 'off'], ['Reader Settings', 'off']]) + K.more('down')),
    S('Font', 'FontPickerActivity', '',
      K.tp({ t: 'Font' }) + L([['ChareInk', 'selected', true], ['Bitter'], ['EB Garamond'], ['Literata']])),
    S('Installed fonts', 'InstalledFontsActivity', 'Size and weight as the italic line.',
      K.tp({ t: 'Installed fonts' }) + R2('Bitter', '4 sizes, 820 KB', '>', true) + R2('EB Garamond', '4 sizes, 1104 KB', 'in use') + R2('Literata', '6 sizes, 1630 KB', '>')),
    S('Font browser', 'FontDownloadActivity', 'Descriptions as the italic line.',
      K.tp({ t: 'Font browser' }) + R('Download All (1.2 MB)', null, true)
      + R2('Bitter', 'slab serif for screens', 'installed') + R2('EB Garamond', 'classic old-style serif', 'update') + R2('Atkinson Hyperlegible', 'high legibility sans', null)),
    S('Downloading', 'FontDownloadActivity (downloading)', 'The status shape: a title page, one line, a rule.',
      K.tp({ t: 'Font browser' }) + cent(['Downloading Bitter (2/5)', 'retry 1/5'], 330) + `<div style="position:absolute;top:410px;left:0;right:0">${K.rule(55)}</div>` + K.hint('back cancels')),
    S('Language', 'LanguageSelectActivity', 'Drawn in Ubuntu so every script shows; the only list not in Literata.',
      K.tp({ t: 'Language' }) + `<div class="ubu">${L([['English', 'selected', true], ['Bahasa Indonesia'], ['Bosanski'], ['Català'], ['Dansk'], ['Deutsch'], ['Español'], ['Français'], ['Italiano'], ['Lietuvių'], ['Magyar'], ['Nederlands']])}</div>` + K.more('down')),
    S('KOReader sync', 'KOReaderSettingsActivity', 'Account rows over settings over actions.',
      K.tp({ t: 'KOReader sync' })
      + L(['#Account', ['Username', 'not set', true], ['Password', 'not set'], ['Sync Server URL', 'default'], '#Sync', ['Document Matching', 'filename'], ['Send Metadata', 'off'], ['Sync Behavior', 'smart sync'], '#Server', ['Sign Up', 'set credentials first'], ['Authenticate', 'set credentials first']])),
    S('KOReader auth', 'KOReaderAuthActivity', 'Status shape.',
      K.tp({ t: 'KOReader auth' }) + cent(['Successfully authenticated!', 'KOReader sync is ready to use'])),
    S('OPDS servers', 'OpdsServerListActivity', '',
      K.tp({ t: 'OPDS servers' }) + R2('Calibre at home', 'http://192.168.1.10:8080/opds', '>', true) + R('Add Server', '>')
      + K.head(1, 'Downloads') + R2('Download folder', 'SD root') + R2('Filename format', 'Author - Title')),
    S('OPDS server', 'OpdsSettingsActivity', 'Values as the italic line, since URLs are long.',
      K.tp({ t: 'Add server', a: 'for calibre, add /opds to your URL' })
      + R2('Server Name', 'not set', null, true) + R2('OPDS Server URL', 'not set') + R2('Username', 'not set') + R2('Password', 'not set')),
    S('Update available', 'OtaUpdateActivity (confirm)', 'Versions as rows with leaders; the two buttons as rows.',
      K.tp({ t: 'Update', a: 'new update available' }) + L([['Current version', '0.35.0'], ['New version', '0.36.0'], ['Update', null, true], 'Cancel'])),
    S('Updating', 'OtaUpdateActivity (installing)', '',
      K.tp({ t: 'Update' }) + cent(['Updating…', '812 KB / 2.4 MB'], 330) + `<div style="position:absolute;top:410px;left:0;right:0">${K.rule(34)}</div>` + K.hint('do not power off')),
    S('Clear cache', 'ClearCacheActivity (warning)', 'Warning as italic prose; choice as a card.',
      K.tp({ t: 'Clear reading cache' }) + `<div class="tips">This will clear all cached book data. All reading progress will be lost! Books will need to be re-indexed when opened again.</div>`
      + K.card(K.head(0, 'Clear reading cache') + L([['Cancel', null, true], 'Clear']), 420)),
  ] },
  { name: 'Network', screens: [
    S('File transfer', 'NetworkModeSelectionActivity', 'Four rows with their subtitles as the italic line.',
      K.tp({ t: 'File transfer' }) + R2('Join a Network', 'use the Wi-Fi you are on', '>', true) + R2('Calibre Wireless', 'send from Calibre on a computer', '>')
      + R2('Create Hotspot', 'the reader makes its own network', '>') + R2('Nearby Reader', 'swap with another lector', '>')),
    S('Wi-Fi networks', 'WifiSelectionActivity', 'Signal and lock as words; the "* = Encrypted" legend goes.',
      K.tp({ t: 'Wi-Fi networks', a: '5 networks found' })
      + L([['CasaDias', 'saved, strong', true], ['MEO-4F2A1', 'locked, good'], ['Vodafone-Home', 'locked, weak'], ['cafe-central', 'open, weak'], ['NOS-12AB', 'locked, weak']])
      + K.fn('mac 64:e8:33:1a:2b:3c')),
    S('Browser transfer', 'CrossPointWebServerActivity (joined)', 'Address in bold, the alternative in italic.',
      K.tp({ t: 'File transfer', a: 'open this address in your browser' }) + `<div style="margin-top:34px">${K.qr(260, 11)}</div>`
      + `<div class="url">http://192.168.1.42/</div><div class="tips" style="margin-top:4px">or http://crosspoint.local/</div>`),
    S('Hotspot', 'CrossPointWebServerActivity (hotspot)', 'Two numbered headings, one code each.',
      K.tp({ t: 'Hotspot' }) + K.head(1, 'Join the network') + `<div style="margin-top:10px">${K.qr(170, 5)}</div>`
      + K.head(2, 'Open the address') + `<div style="margin-top:10px">${K.qr(170, 9)}</div>`),
    S('Calibre wireless', 'CalibreConnectActivity', 'Setup steps under a heading, status under another.',
      K.tp({ t: 'Calibre wireless' })
      + K.head(1, 'Setup') + `<div class="steps"><p>1. Open Calibre on the computer.</p><p>2. Connect/share › Start wireless device connection.</p><p>3. Keep this screen open.</p></div>`
      + K.head(2, 'Status') + `<div class="tips" style="text-align:left;margin:4px 36px 12px">Receiving: Dune.epub</div>` + K.rule(62)),
    S('Nearby reader', 'NearbyFileTransferActivity (peers)', '',
      K.tp({ t: 'Nearby reader', a: 'looking for readers' }) + L([['Lector-4F2A', '>', true], ['Lector-9C01', '>']])),
    S('Incoming file', 'NearbyFileTransferActivity (offer)', '',
      K.tp({ t: 'Nearby reader' }) + K.card(`<div class="cardt">Lector-9C01 sends</div><div class="tips" style="margin-top:0">Hyperion.epub · 1.2 MB</div>` + L([['Accept', null, true], 'Decline']), 280)),
    S('Transferring', 'NearbyFileTransferActivity (transferring)', '',
      K.tp({ t: 'Nearby reader' }) + cent(['Receiving Hyperion.epub', '640 KB of 1.2 MB'], 330) + `<div style="position:absolute;top:410px;left:0;right:0">${K.rule(53)}</div>`),
    S('OPDS catalogue', 'OpdsBookBrowserActivity', 'Books with the author as the italic line; paging rows last.',
      K.tp({ t: 'Calibre at home', a: 'new books' })
      + R2('The Dispossessed', 'Ursula K. Le Guin', '>', true) + R2('Piranesi', 'Susanna Clarke', '>') + R2('The Book of the New Sun', 'Gene Wolfe', '>')
      + R('Next Page', '>')),
    S('KOReader sync result', 'KOReaderSyncActivity', 'The comparison as two headings, choices as a third.',
      K.tp({ t: 'Sync progress', a: 'Middlemarch' })
      + L(['#This reader', ['Place', 'chapter xix · 23%'], '#Remote', ['Place', 'chapter xxii · 27%'], ['Device', 'kobo clara'], '#Choose', ['Apply remote', null, true], 'Upload local'])),
    S('Nearby sync', 'NearbyPositionSyncActivity', 'Status shape.',
      K.tp({ t: 'Nearby sync', a: 'Middlemarch' }) + cent(['Position sent', 'Lector-9C01 is at 23%'])),
  ] },
];

// Render: one numbered panel per screen, grouped, with a nav bar.
(() => {
  let n = 0;
  const nav = document.getElementById('nav'), main = document.getElementById('main');
  for (const g of GROUPS) {
    const id = g.name.toLowerCase().replace(/\W+/g, '-');
    nav.insertAdjacentHTML('beforeend', `<a href="#${id}">${esc(g.name)} · ${g.screens.length}</a>`);
    const panels = g.screens.map((s) => `<div class="opt"><div class="dev"><span class="num">${++n}</span><div class="scr">${s.body}</div></div>`
      + `<div class="cap"><h3>${n}. ${esc(s.title)} <small>${esc(s.src)}</small></h3>${s.note ? `<p>${esc(s.note)}</p>` : ''}</div></div>`).join('');
    main.insertAdjacentHTML('beforeend', `<section class="group" id="${id}"><h2>${esc(g.name)}</h2><div class="grid">${panels}</div></section>`);
  }
  const io = new IntersectionObserver((es) => es.forEach((e) => { if (e.isIntersecting) { e.target.classList.add('in'); io.unobserve(e.target); } }), { rootMargin: '0px 0px -40px 0px' });
  document.querySelectorAll('.opt').forEach((o, i) => { o.style.transitionDelay = `${(i % 4) * 70}ms`; io.observe(o); });
  document.querySelectorAll('.dev').forEach((d) => d.addEventListener('click', () => { d.classList.remove('flash'); void d.offsetWidth; d.classList.add('flash'); }));
})();
