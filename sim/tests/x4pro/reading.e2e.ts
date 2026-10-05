import { test, expect } from 'e2e';

// Open a book by touch and turn its pages with swipes, the X4 Pro's default reader control.
test('opens a book and swipes through its pages', async ({ app, screen }) => {
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
});
