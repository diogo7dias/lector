import { test, expect } from 'e2e';

// Leaving a book files it into /recents, which renames its cache dir (keyed by the book's
// path). The laid-out pages still name the old dir for their images, so reopening the book
// must find each image in the new dir, not draw an empty placeholder box.
test('an image page draws the same after the book is filed into recents', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const frames = screen.getByTestId('frames');
  const busy = screen.getByTestId('busy');
  await expect(activity).toHaveText('Home', { timeout: 120_000 });

  const panel = screen.getByRole('img', { name: 'Device screen' });
  const tapOnce = async (x: number, y: number) => {
    await panel.tap({ position: { x, y } });
    await expect(busy).toHaveText('no', { timeout: 30_000 });
  };
  await tapOnce(240, 517); // Browse Files
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
  await tapOnce(150, 248); // test_jpeg_images.epub, at the card root
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
  await expect(busy).toHaveText('no', { timeout: 30_000 });

  const box = await panel.boundingBox();
  if (!box) throw new Error('no panel on the page');
  const before = await frames.textContent();
  await screen.swipe({ from: { x: box.x + 420, y: box.y + 420 }, to: { x: box.x + 100, y: box.y + 420 } });
  await expect(frames).not.toHaveText(before ?? '', { timeout: 30_000 });
  await expect(busy).toHaveText('no', { timeout: 60_000 });
  // The page body as ink per 20 px band, above the status bar (its "+N" resets on reopen).
  const body = async () => {
    const bands: number[] = [];
    for (let y = 0; y < 740; y += 20) {
      bands.push((await (await fetch(`${app.baseUrl}/ink/0/${y}/480/20`)).json()).ink);
    }
    return bands;
  };
  await expect.poll(async () => Number(await screen.getByTestId('ink').textContent())).toBeGreaterThan(300_000);
  const imagePage = await body();

  await screen.getByRole('button', { name: 'Home' }).click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
  await expect.poll(async () => (await fetch(`${app.baseUrl}/log`)).text()).toContain('-> /recents/test_jpeg_images.epub');

  await tapOnce(240, 259); // the book's row on Home
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
  await expect.poll(body, { message: 'the image page reopens with its image', timeout: 60_000 }).toEqual(imagePage);
  expect(await (await fetch(`${app.baseUrl}/log`)).text()).not.toContain('Image file not found');

  // The suite shares one card: Remove from Recents files the book back to the root and
  // returns Home, so the next test finds the card and the screen as they were.
  await panel.tap({ position: { x: 240, y: 400 } }); // centre of the page
  await expect(activity).toHaveText('EpubReaderMenu', { timeout: 30_000 });
  await tapOnce(100, 198); // Book
  await tapOnce(100, 318); // Remove from Recents
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
  await expect.poll(async () => (await fetch(`${app.baseUrl}/log`)).text()).toContain('-> /test_jpeg_images.epub');
});
