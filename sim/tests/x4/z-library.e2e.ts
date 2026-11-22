import { test, expect } from 'e2e';

// Home > Library lists every book on the card by title, each once. Opening one files it into
// /recents; the next visit lists the same books, still once each, and opens it from its new path.
// Named to run last on the board: opening a book reorders Home's recents for every file after it.
// Starts where toolbar.e2e.ts leaves Home, on its Settings row.
test('Library lists the card, follows a book into /recents and opens it again', async ({ app, screen }) => {
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

  // Up from Settings: Quick Panel, Nearby Sync, OPDS Browser, Library. Each visit after the
  // first starts there too: a book's Back returns to the Library, whose Back lands Home on its row.
  await press('Up', 4);
  // known: the book count of the visit before. A visit with no book change skips the rebuild.
  const visit = async (known?: number) => {
    const from = (await log()).length;
    await press('Confirm');
    await expect(activity).toHaveText('Library', { timeout: 30_000 });
    await expect
      .poll(async () => (await log()).slice(from).includes(known === undefined ? 'built:' : '] row 0: '), { timeout: 60_000 })
      .toBe(true);
    const read = async () => {
      const text = (await log()).slice(from);
      const books = Number(/built: (\d+) books/.exec(text)?.[1] ?? known);
      // Title per row index, from every window the screen loaded during this visit.
      const byRow = new Map<number, string>();
      for (const m of text.matchAll(/\[LIB\] row (\d+): (.*?) \|/g)) byRow.set(Number(m[1]), m[2]);
      const titles = [...byRow.keys()].sort((x, y) => x - y).map((k) => byRow.get(k) as string);
      return { books, titles, from };
    };
    // The window logs row by row: wait until every book of the card has its line.
    try {
      await expect.poll(async () => (await read()).titles.length, { timeout: 30_000 }).toBe((await read()).books);
    } catch (e) {
      const lines = (await log()).slice(from).split('\n').filter((l) => l.includes('LIB'));
      throw new Error(`${(e as Error).message}\n${lines.join('\n')}`);
    }
    return read();
  };
  const openFirst = async (from: number) => {
    await press('Down'); // past the Search all books row, onto the first book
    await press('Confirm');
    await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
    const path = /\[LIB\] open (\S+)/.exec((await log()).slice(from))?.[1];
    await press('Back');
    await expect(activity).toHaveText('Library', { timeout: 30_000 });
    await press('Back');
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
    return path as string;
  };

  const first = await visit();
  expect(first.books, 'the card holds the two test books').toBeGreaterThanOrEqual(2);
  expect(first.titles).toHaveLength(first.books);
  expect(new Set(first.titles).size, 'no book twice').toBe(first.titles.length);
  expect(first.titles, 'sorted by title').toEqual([...first.titles].sort((a, b) => a.toLowerCase().localeCompare(b.toLowerCase())));
  const openedAt = await openFirst(first.from);
  expect(openedAt, 'a path on the card').toMatch(/^\/.+\.epub$/);

  // Opening filed the book into /recents (or it was there already); the index must follow it.
  const second = await visit(first.books);
  expect(second.books, 'same books, none lost or doubled by the move').toBe(first.books);
  expect(second.titles).toEqual(first.titles);
  const reopenedAt = await openFirst(second.from);
  expect(reopenedAt, 'the row names the book where it is now').toMatch(/^\/recents\/.+\.epub$/);
  if (openedAt !== reopenedAt) expect(await log()).toContain(`Filing book: ${openedAt} -> ${reopenedAt}`);
});
