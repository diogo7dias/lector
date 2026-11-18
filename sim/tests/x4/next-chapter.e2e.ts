import { test, expect } from 'e2e';

// Near a chapter's end the reader lays out the next one while idle, so turning into it loads
// a finished cache instead of building on the turn. Reads the JPEG book (ten one- or two-page
// chapters), the last book read; leaves the device on Home, on Browse Files, for the reading suite.
test('the next chapter is laid out before the turn into it', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  await expect(activity).toHaveText('Home', { timeout: 60_000 });

  const press = async (name: string, times = 1) => {
    for (let i = 0; i < times; i++) {
      await screen.getByRole('button', { name }).click();
      await expect(busy).toHaveText('no', { timeout: 60_000 });
    }
  };
  const log = async () => (await fetch(`${app.baseUrl}/log`)).text();
  const start = (await log()).length;
  const since = async () => (await log()).slice(start);
  const opened = (text: string) => [...text.matchAll(/Loading file: \S+, index: (\d+)/g)].map((m) => Number(m[1]));

  // Home's book row, above Browse Files: the JPEG book, the last one read (All Bookmarks).
  await press('Up');
  await press('Confirm');
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });

  for (let turn = 0; turn < 4; turn++) {
    const current = opened(await since()).at(-1);
    expect(current, 'a chapter is open').toBeDefined();
    await expect
      .poll(async () => (await since()).includes(`Next chapter ${current! + 1} ready:`), { timeout: 60_000 })
      .toBe(true);
    await press('Right');
  }

  const text = await since();
  const chapters = opened(text);
  expect(chapters.length, 'the turns crossed into later chapters').toBeGreaterThan(1);
  // Everything after the book's own first open came from a prefetched cache.
  const afterFirst = text.slice(text.indexOf('Loading file:') + 1);
  expect(afterFirst).not.toContain('Cache not found, building...');

  await press('Back');
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
  await press('Down'); // Home comes back on the book's row; the next file expects Browse Files
});
