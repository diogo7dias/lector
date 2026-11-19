import { test, expect } from 'e2e';

// Settings > Reader > Reader Menu Style > Toolbar: Confirm in a book paints a sheet over the
// bottom of the page instead of opening the list menu. Down steps a chapter, Back closes it.
test('the toolbar menu opens, steps a chapter and closes by the buttons', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  await expect(activity).not.toHaveText('', { timeout: 60_000 });
  if ((await activity.textContent()) !== 'Home') {
    await screen.getByRole('button', { name: 'Back' }).click(); // an earlier suite left a book open
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
  }

  const press = async (name: string, times = 1) => {
    for (let i = 0; i < times; i++) {
      await screen.getByRole('button', { name }).click();
      await expect(busy).toHaveText('no', { timeout: 60_000 });
    }
  };
  const ink = async (x: number, y: number, w: number, h: number) =>
    (await (await fetch(`${app.baseUrl}/ink/${x}/${y}/${w}/${h}`)).json()).ink as number;
  // Home starts on its first row, and Up from there wraps to Settings, the last: the same two
  // presses whether or not a book is already listed above the menu.
  const setStyle = async () => {
    await press('Up'); // Settings
    await press('Confirm');
    await expect(activity).toHaveText('Settings', { timeout: 30_000 });
    await press('Down'); // Reader
    await press('Confirm');
    await press('Down', 11); // Reader Menu Style
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

  // The sheet's band, y 583-800, is mostly white around a thin scrub row; the page that fills it
  // without the sheet is far denser.
  const band = () => ink(0, 590, 480, 210);
  const withoutSheet = await band();
  await press('Confirm');
  expect(await activity.textContent(), 'a sheet, not the list menu').toBe('EpubReader');
  await expect.poll(band, { timeout: 30_000 }).toBeLessThan(withoutSheet / 2);

  // Down steps one chapter: the page above the sheet changes, the sheet stays.
  const before = await ink(0, 0, 480, 580);
  await press('Down');
  await expect.poll(() => ink(0, 0, 480, 580), { timeout: 60_000 }).not.toBe(before);
  await expect.poll(band, { timeout: 30_000 }).toBeLessThan(withoutSheet / 2);

  // Back closes it, the page is whole again; Confirm opens it once more.
  await press('Back');
  await expect.poll(band, { timeout: 60_000 }).toBeGreaterThan(withoutSheet / 2);
  await press('Confirm');
  await expect.poll(band, { timeout: 30_000 }).toBeLessThan(withoutSheet / 2);
  await press('Back'); // closed again: the page, for the next test
  await expect.poll(band, { timeout: 60_000 }).toBeGreaterThan(withoutSheet / 2);
});

// Confirm on the marked tile (Contents) opens the chapter list; Back returns to the page. Then
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
  await press('Confirm'); // the sheet, the Contents tile marked
  await press('Confirm');
  await expect(activity).toHaveText('EpubReaderChapterSelection', { timeout: 30_000 });
  await press('Back');
  await expect
    .poll(async () => (await (await fetch(`${app.baseUrl}/log`)).text()).includes('Exiting activity: EpubReaderChapterSelection'))
    .toBe(true);

  // Leave the book (Back lands on the browser or on Home, by setting). Up from Home's first row
  // wraps to Settings.
  await press('Back');
  await expect.poll(async () => await activity.textContent(), { timeout: 30_000 }).toMatch(/^(Home|FileBrowser)$/);
  if ((await activity.textContent()) === 'FileBrowser') await press('Back');
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
  await press('Up'); // Settings
  await press('Confirm');
  await expect(activity).toHaveText('Settings', { timeout: 30_000 });
  await press('Down'); // Reader
  await press('Confirm');
  await press('Down', 11); // Reader Menu Style
  await press('Confirm'); // Toolbar -> List
  await press('Back');
  await press('Back');
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});
