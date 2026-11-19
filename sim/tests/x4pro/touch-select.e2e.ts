import { test, expect } from 'e2e';

// Hold a word and drag on: the selection follows the finger down the page, the hint bar
// turns into Cancel / Save / Look Up, a second drag moves the end, and Save writes the
// passage to the book's quotes file and to /My Clippings.txt.
test('holds a word, drags across lines, saves the passage as a quote', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  await expect(activity).not.toHaveText('', { timeout: 120_000 });
  if ((await activity.textContent()) !== 'Home') {
    await screen.getByRole('button', { name: 'Home' }).click(); // an earlier suite left a book open
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
  }

  const panel = screen.getByRole('img', { name: 'Device screen' });
  const tapTwice = async (x: number, y: number) => {
    for (let i = 0; i < 2; i++) {
      await panel.tap({ position: { x, y } });
      await expect(busy).toHaveText('no', { timeout: 30_000 });
    }
  };
  const log = async () => (await fetch(`${app.baseUrl}/log`)).text();
  const ink = async (y: number, h: number) => (await (await fetch(`${app.baseUrl}/ink/0/${y}/480/${h}`)).json()).ink;
  // The suite shares one card: an earlier test may already have filed the book into
  // Recents (Home then shows it as its first row, blank on a fresh card) and left it on
  // the contents page. A fresh open starts on the cover: turn one page to the contents.
  const box = await panel.boundingBox();
  if (!box) throw new Error('no panel on the page');
  const resume = (await ink(230, 60)) > 5_000;
  if (resume) {
    await tapTwice(240, 260); // the book's row on Home
  } else {
    await tapTwice(240, 517); // Browse Files
    await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
    await tapTwice(150, 279); // test_kerning_ligature.epub
  }
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
  await expect(busy).toHaveText('no', { timeout: 30_000 });
  if (!resume) {
    await screen.swipe({ from: { x: box.x + 420, y: box.y + 420 }, to: { x: box.x + 100, y: box.y + 420 } });
    await expect(busy).toHaveText('no', { timeout: 60_000 });
  }

  const post = (path: string) => fetch(`${app.baseUrl}${path}`, { method: 'POST' });
  // The contents title is drawn at the nearest built-in ChareInk size to its CSS scale, so at
  // 16 pt it wraps to a second line and every entry sits one line (32 px) lower than at 14 pt.
  const dy = 32;

  // 1.2 s still (the long press), then 1.5 s down two entries, then lift.
  await post(`/swipe/240/${300 + dy}/240/${420 + dy}?hold=1200&ms=1500`);
  await expect(activity).toHaveText('QuoteSelect', { timeout: 30_000 });
  await expect(busy).toHaveText('no', { timeout: 30_000 });
  // Bars over the dragged lines (entries 3 to 5), plain text on entries 7 to 9 below.
  await expect.poll(async () => (await ink(280 + dy, 120)) - 2 * (await ink(560 + dy, 120)), {
    message: 'the drag selected several lines', timeout: 30_000,
  }).toBeGreaterThan(0);
  expect(await log(), 'the lift neither saved nor looked anything up').not.toContain('Saved quote');

  // Touch just below the last selected word and drag that end on, to entry 7.
  const plain = await ink(560 + dy, 120);
  await post(`/swipe/200/${440 + dy}/300/${600 + dy}?ms=1200`);
  await expect.poll(() => ink(560 + dy, 120), { message: 'the end moved down', timeout: 30_000 })
    .toBeGreaterThan(plain * 2);

  await post('/tap/180/785?ms=150'); // Save, the second hint
  await expect.poll(log, { message: 'Save wrote the quote', timeout: 30_000 }).toContain('Saved quote');
  await expect.poll(log, { message: 'Save added the Kindle clipping', timeout: 30_000 })
    .toContain('Clipping added to /My Clippings.txt');
});
