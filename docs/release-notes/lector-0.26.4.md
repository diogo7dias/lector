## Added
- **The selection highlight has a style.** Display offers Solid (the old filled black row, still the default), Brackets (corner marks around a row's own label and value) and Caret (an arrow and a rule). It applies to every menu, list, popup and tab.
- **One margin for all four sides is back.** Link Top/Bottom becomes **Link Margins**: Off keeps Top Margin and Bottom Margin apart, Top+Bottom gives one Vertical Margin, All Sides puts a single number on every side. All Sides turns Dynamic Margins off and stops offering it, because Dynamic Margins works out a horizontal margin of its own. New devices start on All Sides at 20; existing settings and presets keep the margins and link they had, including anyone still on the old Uniform Margins switch.
- **Nine font sizes on SD families, and five more families.** Every downloadable family now ships point sizes 10 through 18 instead of 12/14/16/18 (OpenDyslexic runs 8 through 16, its glyphs being larger per point). New: ChareInk, XCharter, Newsreader, LexendDeca, AnonymousPro.
- **Tables with merged cells lay out correctly.** A row holding a colspan or rowspan cell flows stacked and full width, a rowspan carries down the rows beneath it, and text alignment set on the table, row, body, head, foot, column group or caption now reaches the cell text.

## Fixed
- **Sleep screens no longer ghost on the X3.** The panel's BUSY idle wait, dropped by the 0.26.3 stock-driver revert, is back; the timing tuning stays out.
- **No white band above the status bar on the X3.** Bezel insets come from the board profile instead of the X4's measurements.
- **The Continue Reading cover no longer fills solid black** once the selector moves off it, and it is marked once rather than three times.
- **The nearby peer list, the OPDS browser and the XTC chapter list follow the selection style** like every other list.
- **Favoriting a wallpaper from the image viewer shows up in the file browser.** The row asked the queue using its display label, and a failed open could leave a doubled slash in the path, so neither the progress file nor the queue answered.
- **Chapters ending with stray data after `</html>` parse again.**

## Note
- Cached page layout is rebuilt on first open of each book, because table cells now place differently.
