import { test, expect } from 'e2e';

// Open a book by touch, turn a page with a swipe (the X4 Pro's default reader control),
// open the in-book menu, and find the same page again after a power cycle.
test('opens a book, swipes a page, opens the menu, resumes after a reboot', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const frames = screen.getByTestId('frames');
  const busy = screen.getByTestId('busy');
  await expect(activity).toHaveText('Home', { timeout: 120_000 });

  const panel = screen.getByRole('img', { name: 'Device screen' });
  const tapTwice = async (x: number, y: number) => {
    for (let i = 0; i < 2; i++) {
      await panel.tap({ position: { x, y } });
      await expect(busy).toHaveText('no', { timeout: 30_000 });
    }
  };
  await tapTwice(240, 570); // Browse Files
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
  await tapTwice(150, 310); // test_kerning_ligature.epub
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
  await expect(busy).toHaveText('no', { timeout: 30_000 });

  const page = async () => (await fetch(`${app.baseUrl}/screen.png`)).arrayBuffer();
  const first = Buffer.from(await page());
  const before = await frames.textContent();
  const box = await panel.boundingBox();
  if (!box) throw new Error('no panel on the page');
  // Finger right to left across the page: next page.
  await screen.swipe({ from: { x: box.x + 420, y: box.y + 420 }, to: { x: box.x + 100, y: box.y + 420 } });
  await expect(frames).not.toHaveText(before ?? '', { timeout: 30_000 });
  await expect(busy).toHaveText('no', { timeout: 60_000 });
  expect(Buffer.from(await page()).equals(first), 'the page changed').toBe(false);
  // A page of text, not a blank or a cleared panel.
  await expect.poll(async () => Number(await screen.getByTestId('ink').textContent())).toBeGreaterThan(5_000);
  // The page body as ink per 20 px band, stopping above the status bar: its "+N"
  // counts pages turned this session and starts again at +0 after a reboot.
  const body = async () => {
    const bands: number[] = [];
    for (let y = 0; y < 740; y += 20) {
      bands.push((await (await fetch(`${app.baseUrl}/ink/0/${y}/480/20`)).json()).ink);
    }
    return bands;
  };
  const second = await body();

  await panel.tap({ position: { x: 240, y: 400 } }); // centre of the page
  await expect(activity).toHaveText('EpubReaderMenu', { timeout: 30_000 });
  await screen.getByRole('button', { name: 'Home' }).click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });

  await screen.getByRole('button', { name: 'Reboot' }).click();
  await expect(busy).toHaveText('no', { timeout: 180_000 });
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
  // The activity flips to EpubReader before its first page is painted: poll, never one read.
  await expect.poll(body, { message: 'the book reopens on the page it was left at', timeout: 60_000 }).toEqual(second);
});
