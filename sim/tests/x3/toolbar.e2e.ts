import { test, expect, type Locator } from 'e2e';

// Leaves the book (or the browser) for Home and stops on its Settings row. A book's Back
// returns where it was opened: Home, on its first row, or the browser, whose Back lands Home
// on Browse Files. Up from the first row wraps to Settings; from Browse Files it is five rows
// down (Library, OPDS Browser, Nearby Sync, Quick Panel, Settings).
async function homeOnSettings(activity: Locator, press: (name: string, times?: number) => Promise<void>) {
  let viaBrowser = false;
  while ((await activity.textContent()) !== 'Home') {
    viaBrowser ||= (await activity.textContent()) === 'FileBrowser';
    const leaving = await activity.textContent();
    await press('Back');
    await expect(activity).not.toHaveText(leaving ?? '', { timeout: 30_000 });
  }
  await press(viaBrowser ? 'Down' : 'Up', viaBrowser ? 5 : 1);
}

// X3: same flow as the X4 (shared binary, same button ladder), on its 528x792 panel.
// Settings > Reader > Reader Menu Style > Toolbar: Confirm in a book paints a sheet over the
// bottom of the page instead of opening the list menu. Down steps a chapter, Back closes it.
test('the toolbar menu opens, steps a chapter and closes by the buttons', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  await expect(activity).not.toHaveText('', { timeout: 60_000 });

  const press = async (name: string, times = 1) => {
    for (let i = 0; i < times; i++) {
      await screen.getByRole('button', { name }).click();
      await expect(busy).toHaveText('no', { timeout: 60_000 });
    }
  };
  await homeOnSettings(activity, press); // an earlier suite may have left a book open
  const ink = async (x: number, y: number, w: number, h: number) =>
    (await (await fetch(`${app.baseUrl}/ink/${x}/${y}/${w}/${h}`)).json()).ink as number;
  // Starts on Home's Settings row.
  const setStyle = async () => {
    await press('Confirm');
    await expect(activity).toHaveText('Settings', { timeout: 30_000 });
    await press('Down'); // Reader
    await press('Confirm');
    await press('Down', 12); // Reader Menu Style
    await press('Confirm'); // List <-> Toolbar
    await press('Back'); // to the tabs
    await press('Back'); // to Home
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
    await press('Down'); // Home remembers Settings; Down wraps to the first row
  };

  await setStyle();
  await press('Confirm');
  // The first row is the book itself once one is listed, else Browse Files.
  await expect.poll(async () => await activity.textContent(), { timeout: 60_000 }).toMatch(/^(FileBrowser|EpubReader)$/);
  if ((await activity.textContent()) === 'FileBrowser') {
    await press('Up'); // the last book in the card's root
    await press('Confirm');
  }
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
  await expect(busy).toHaveText('no', { timeout: 30_000 });

  // The sheet's band, y 575-792, is mostly white around a thin scrub row; the page that fills it
  // without the sheet is far denser.
  const band = () => ink(0, 600, 528, 190);
  const withoutSheet = await band();
  await press('Confirm');
  expect(await activity.textContent(), 'a sheet, not the list menu').toBe('EpubReader');
  await expect.poll(band, { timeout: 30_000 }).toBeLessThan(withoutSheet / 2);

  // Down steps one chapter: the page above the sheet changes, the sheet stays.
  const before = await ink(0, 0, 528, 570);
  await press('Down');
  await expect.poll(() => ink(0, 0, 528, 570), { timeout: 60_000 }).not.toBe(before);
  await expect.poll(band, { timeout: 30_000 }).toBeLessThan(withoutSheet / 2);

  // Back closes it, the page is whole again; Confirm opens it once more.
  await press('Back');
  await expect.poll(band, { timeout: 60_000 }).toBeGreaterThan(withoutSheet / 2);
  await press('Confirm');
  await expect.poll(band, { timeout: 30_000 }).toBeLessThan(withoutSheet / 2);
  await press('Back'); // closed again: the page, for the next test
  await expect.poll(band, { timeout: 60_000 }).toBeGreaterThan(withoutSheet / 2);
});

// Confirm on the marked tile (Contents) opens the chapter list; Back returns to the sheet, and
// Back again to the page. Then
// leaves the book and puts the list style back, so the device is on Home for the next file.
test('the toolbar menu opens a tile by Confirm, and the list style comes back', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  await expect(activity).toHaveText('EpubReader', { timeout: 30_000 });

  const press = async (name: string, times = 1) => {
    for (let i = 0; i < times; i++) {
      await screen.getByRole('button', { name }).click();
      await expect(busy).toHaveText('no', { timeout: 60_000 });
    }
  };
  const band = async () =>
    (await (await fetch(`${app.baseUrl}/ink/0/600/528/190`)).json()).ink as number;
  const withoutSheet = await band();
  await press('Confirm'); // the sheet, the Contents tile marked
  await press('Confirm');
  await expect(activity).toHaveText('EpubReaderChapterSelection', { timeout: 30_000 });
  await press('Back');
  await expect
    .poll(async () => (await (await fetch(`${app.baseUrl}/log`)).text()).includes('Exiting activity: EpubReaderChapterSelection'))
    .toBe(true);
  // Back from the tile's screen brings the sheet back over the page; Back closes it.
  await expect.poll(band, { timeout: 60_000 }).toBeLessThan(withoutSheet / 2);
  await press('Back');
  await expect.poll(band, { timeout: 60_000 }).toBeGreaterThan(withoutSheet / 2);

  await homeOnSettings(activity, press);
  await press('Confirm');
  await expect(activity).toHaveText('Settings', { timeout: 30_000 });
  await press('Down'); // Reader
  await press('Confirm');
  await press('Down', 12); // Reader Menu Style
  await press('Confirm'); // Toolbar -> List
  await press('Back');
  await press('Back');
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});
