import { test, expect } from 'e2e';

// Home > Library by touch: one tap opens a row. Opening a book files it
// into /recents; the next visit lists the same number of books and opens it from its new path.
// Named to run last on the board: opening a book reorders Home's recents for every file after it.
test('Library opens by touch and follows a book into /recents', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  const panel = screen.getByRole('img', { name: 'Device screen' });
  await expect(activity).not.toHaveText('', { timeout: 120_000 });
  if ((await activity.textContent()) !== 'Home') {
    await screen.getByRole('button', { name: 'Home' }).click();
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
  }
  const tapOnce = async (x: number, y: number) => {
    await panel.tap({ position: { x, y } });
    await expect(busy).toHaveText('no', { timeout: 30_000 });
  };
  const log = async () => (await fetch(`${app.baseUrl}/log`)).text();

  const visit = async () => {
    const from = (await log()).length;
    await tapOnce(240, 570); // Library, under Browse Files
    await expect(activity).toHaveText('Library', { timeout: 30_000 });
    await expect.poll(async () => (await log()).slice(from).includes('built:'), { timeout: 60_000 }).toBe(true);
    const books = Number(/built: (\d+) books/.exec((await log()).slice(from))?.[1]);
    return { books, from };
  };
  const openFirst = async (from: number) => {
    await tapOnce(240, 88); // the first row: the title that sorts first
    await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
    const path = /\[LIB\] open (\S+)/.exec((await log()).slice(from))?.[1];
    await screen.getByRole('button', { name: 'Home' }).click();
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
    return path as string;
  };

  const first = await visit();
  expect(first.books, 'the card holds the two test books').toBeGreaterThanOrEqual(2);
  // Two or three one-line rows from the top of the list, and nothing under them.
  expect((await (await fetch(`${app.baseUrl}/ink/0/60/480/90`)).json()).ink as number).toBeGreaterThan(1_000);
  const openedAt = await openFirst(first.from);
  expect(openedAt).toMatch(/^\/.+\.epub$/);

  const second = await visit();
  expect(second.books, 'same books after the move').toBe(first.books);
  const reopenedAt = await openFirst(second.from);
  expect(reopenedAt, 'the row names the book where it is now').toMatch(/^\/recents\/.+\.epub$/);
  if (openedAt !== reopenedAt) expect(await log()).toContain(`Filing book: ${openedAt} -> ${reopenedAt}`);
});
