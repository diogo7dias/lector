import { test, expect } from 'e2e';

// Bookmark a page, read on, then open the book from Settings > Reader > All Bookmarks: it
// lands on the bookmark, not where reading stopped. Leaves the device on Home, on Browse Files.
test('All Bookmarks opens the book at the bookmark', async ({ app, screen }) => {
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
  const lastMatch = (text: string, re: RegExp) => [...text.matchAll(re)].at(-1);

  await press('Confirm');
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
  // The JPEG book, one above the last: the other files read the last book and expect it
  // unread, with no bookmarks.
  await press('Up', 2);
  await press('Confirm');
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
  await press('Right', 3);

  // Reader menu > Navigate > Toggle Bookmark, the fifth row while the book has none.
  await press('Confirm');
  await expect(activity).toHaveText('EpubReaderMenu', { timeout: 30_000 });
  await press('Confirm');
  await press('Down', 5);
  await press('Confirm');
  await expect.poll(async () => (await log()).includes('Toggle bookmark at spine')).toBe(true);
  const [, spine, page] = lastMatch(await log(), /Toggle bookmark at spine (\d+), page (\d+)/g) ?? [];

  await press('Right', 2); // read on, so the saved position moves past the bookmark
  await press('Back');
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
  const readOn = lastMatch(await log(), /Progress saved: spine=(\d+) offset=\d+ page=(\d+)/g);
  expect(readOn?.slice(1), 'reading stopped past the bookmark').not.toEqual([spine, page]);

  await press('Down', 5); // Settings, below the book row and the four menu rows above it
  await press('Confirm');
  await expect(activity).toHaveText('Settings', { timeout: 30_000 });
  await press('Down'); // Reader
  await press('Confirm');
  await press('Down', 12); // All Bookmarks, the last row
  await press('Confirm');
  await expect(activity).toHaveText('AllBookmarks', { timeout: 30_000 });
  expect(await log()).toContain('Loaded 1 bookmarks from 1 books');

  const before = (await log()).length;
  await press('Confirm');
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
  await expect.poll(async () => (await log()).slice(before).includes('Rendered page'), { timeout: 60_000 }).toBe(true);

  // Leaving saves where the reader stands: the bookmarked page.
  await press('Back');
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
  const saved = lastMatch((await log()).slice(before), /Progress saved: spine=(\d+) offset=\d+ page=(\d+)/g);
  expect(saved?.slice(1)).toEqual([spine, page]);
  // Home comes back on the book's row; the next file expects Browse Files.
  await press('Down');
});
