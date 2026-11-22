import { test, expect } from 'e2e';

// The touch side of files, Library search and the keyboard (UI rethink PR 4). Each test
// starts and ends on Home; none opens a book, so the card stays as the later files expect.

const setup = async (app: { open: (p: string) => Promise<void>; baseUrl: string }, screen: any) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  await expect(activity).not.toHaveText('', { timeout: 120_000 });
  if ((await activity.textContent()) !== 'Home') {
    await screen.getByRole('button', { name: 'Home' }).click();
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
  }
  const panel = screen.getByRole('img', { name: 'Device screen' });
  const tap = async (x: number, y: number) => {
    await panel.tap({ position: { x, y } });
    await expect(busy).toHaveText('no', { timeout: 30_000 });
  };
  const post = async (path: string) => {
    await fetch(`${app.baseUrl}${path}`, { method: 'POST' });
    await expect(busy).toHaveText('no', { timeout: 30_000 });
  };
  const log = async () => (await fetch(`${app.baseUrl}/log`)).text();
  const ink = async (x: number, y: number, w: number, h: number) =>
    (await (await fetch(`${app.baseUrl}/ink/${x}/${y}/${w}/${h}`)).json()).ink as number;
  const home = async () => {
    await screen.getByRole('button', { name: 'Home' }).click();
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
  };
  return { activity, tap, post, log, ink, home };
};

test('File browser: the header names the order, a long-press opens the file actions, the path goes up', async ({
  app,
  screen,
}) => {
  const { activity, tap, post, log, ink, home } = await setup(app, screen);
  await tap(240, 517); // Browse Files
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
  // "Alphabetical" in the header band, between the folder name and the battery.
  expect(await ink(220, 10, 170, 30), 'the sort order is named in the header').toBeGreaterThan(200);

  // Hold test_jpeg_images.epub (rows: search, [read], [recents], [sleep pause], [sleep], it).
  const from = (await log()).length;
  const listOnly = await ink(80, 290, 320, 220);
  await post('/tap/120/248?ms=1500');
  await expect.poll(async () => (await log()).slice(from), { timeout: 30_000 }).toContain('long-press row 5');
  // The popup sits over the empty half of the list.
  await expect.poll(() => ink(80, 290, 320, 220), { timeout: 30_000 }).toBeGreaterThan(listOnly + 1_000);
  await tap(60, 780); // Back closes the popup
  await expect.poll(() => ink(80, 290, 320, 220), { timeout: 30_000 }).toBe(listOnly);

  await tap(80, 124); // [read]
  const beforeUp = (await log()).length;
  await tap(100, 738); // the path bar
  await expect.poll(async () => (await log()).slice(beforeUp), { timeout: 30_000 }).toContain('path tap: up from /read');
  await expect(activity).toHaveText('FileBrowser');
  await home();
});

test('Library searches every book, and a tap in the field moves the cursor', async ({ app, screen }) => {
  const { activity, tap, log, ink, home } = await setup(app, screen);
  await tap(240, 570); // Library
  await expect(activity).toHaveText('Library', { timeout: 30_000 });
  await tap(240, 88); // Search all books
  await expect(activity).toHaveText('KeyboardEntry', { timeout: 30_000 });

  // Type "ig", tap the start of the field, type "l": the l lands first, so the query is "lig".
  await tap(351, 520); // i
  await tap(239, 582); // g
  const from = (await log()).length;
  await tap(40, 108); // the field, left of the first letter
  await expect.poll(async () => (await log()).slice(from), { timeout: 30_000 }).toContain('cursor to 0');
  await tap(404, 582); // l
  await tap(420, 705); // OK: back on the Library (a return logs no new activity name)
  await expect.poll(async () => (await log()).slice(from), { timeout: 30_000 }).toContain('search "lig": 1 match');
  // Search row (two lines), Clear search, and the one book: nothing under it.
  expect(await ink(0, 160, 480, 40), 'the matching book is listed').toBeGreaterThan(500);
  expect(await ink(0, 230, 480, 300), 'and only it').toBe(0);

  // "1 found" under the query; Clear search takes it away with the query.
  const found = await ink(0, 98, 200, 14);
  expect(found, 'the search row counts its matches').toBeGreaterThan(200);
  await tap(240, 137); // Clear search
  // Only the top of the first book's row is left in that band.
  await expect.poll(() => ink(0, 98, 200, 14), { timeout: 30_000 }).toBeLessThan(found / 2);
  expect(await ink(0, 160, 480, 40), 'both books are back').toBeGreaterThan(500);
  await home();
});

test('A password field shows and hides its text on a tap', async ({ app, screen }) => {
  const { activity, tap, log, home } = await setup(app, screen);
  await tap(240, 729); // Settings
  await tap(240, 323); // System
  await tap(100, 553); // KOReader Sync
  await expect(activity).toHaveText('KOReaderSettings', { timeout: 30_000 });
  await tap(100, 128); // Password
  await expect(activity).toHaveText('KeyboardEntry', { timeout: 30_000 });
  await tap(36, 520); // q
  const from = (await log()).length;
  await tap(420, 108); // [abc]
  await expect.poll(async () => (await log()).slice(from), { timeout: 30_000 }).toContain('password shown');
  await tap(420, 108); // [***]
  await expect.poll(async () => (await log()).slice(from), { timeout: 30_000 }).toContain('password hidden');
  await tap(60, 780); // Back: nothing saved
  await home();
});
