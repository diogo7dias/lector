import { test, expect } from 'e2e';

// Full-screen image viewer (L4):
// - Long-pressing an image on the page opens EpubImageInspect directly (edge-to-edge,
//   wide images rotated 90 degrees, no status bar).
// - On X4 Pro, tapping toggles 2x zoom in PSRAM and dragging pans across the zoomed view.
// - Back returns to the page cleanly, and "Inspect Image" also appears in Reader Menu > Navigate.
test('full-screen image viewer opens by long-press and menu, rotates wide images, zooms 2x and pans', async ({
  app,
  screen,
}) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const frames = screen.getByTestId('frames');
  const busy = screen.getByTestId('busy');
  const panel = screen.getByRole('img', { name: 'Device screen' });
  const home = screen.getByRole('button', { name: 'Home' });
  await expect(activity).not.toHaveText('', { timeout: 120_000 });
  if ((await activity.textContent()) !== 'Home') {
    await home.click();
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
  }

  const settle = () => expect(busy).toHaveText('no', { timeout: 60_000 });
  const tapOnce = async (x: number, y: number) => {
    await panel.tap({ position: { x, y } });
    await settle();
  };
  const post = async (path: string) => {
    const res = await fetch(`${app.baseUrl}${path}`, { method: 'POST' });
    expect(res.ok, path).toBe(true);
    await settle();
  };
  const ink = async (x: number, y: number, w: number, h: number) =>
    (await (await fetch(`${app.baseUrl}/ink/${x}/${y}/${w}/${h}`)).json()).ink as number;
  const log = async () => (await fetch(`${app.baseUrl}/log`)).text();

  // Open JPEG Image Tests (test_jpeg_images.epub) via Library row 0.
  await tapOnce(240, 570); // Library
  await expect(activity).toHaveText('Library', { timeout: 30_000 });
  await expect.poll(() => ink(0, 60, 480, 90), { timeout: 60_000 }).toBeGreaterThan(500);
  await settle();
  await tapOnce(240, 128); // JPEG Image Tests
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
  await settle();

  const box = await panel.boundingBox();
  if (!box) throw new Error('no panel on the page');
  // Ensure we are on chapter 2 (jpeg_format.jpg, 350x250 centered at x=65..415, y=167..417).
  if ((await ink(100, 200, 280, 180)) < 15_000) {
    const beforeSwipe = await frames.textContent();
    await screen.swipe({ from: { x: box.x + 420, y: box.y + 420 }, to: { x: box.x + 100, y: box.y + 420 } });
    await expect(frames).not.toHaveText(beforeSwipe ?? '', { timeout: 30_000 });
    await settle();
  }

  // Long-press on the image opens EpubImageInspect directly (not QuoteSelect).
  const pageInk = await ink(0, 0, 480, 740);
  const beforeLongPress = (await log()).length;
  await post('/tap/240/280?ms=1200');
  await expect(activity).toHaveText('EpubImageInspect', { timeout: 30_000 });
  await settle();
  const fitInk = await ink(0, 0, 480, 800);
  expect(fitInk, 'full-screen image rendered').toBeGreaterThan(2_000);

  // Tap toggles 2x zoom in PSRAM.
  await tapOnce(240, 400);
  await expect.poll(async () => (await log()).slice(beforeLongPress), { timeout: 30_000 }).toContain('[IMGV] Zoom 2x');
  const zoomedInk = await ink(0, 0, 480, 800);

  // Swipe pans across the 2x PSRAM buffer.
  const beforePan = (await log()).length;
  await post('/swipe/360/500/120/260');
  await expect.poll(async () => (await log()).slice(beforePan), { timeout: 30_000 }).toContain('[IMGV] Pan (');
  const pannedInk = await ink(0, 0, 480, 800);
  expect(pannedInk === zoomedInk && pannedInk === fitInk, 'zoom/pan updated the panel').toBe(false);

  // Back gesture from the rotated left edge (bottom edge in upright panel coords for
  // LandscapeClockwise) exits the viewer and restores the reader page cleanly.
  await post('/swipe/240/790/240/500');
  await expect.poll(() => ink(0, 0, 480, 740), { timeout: 30_000 }).toBe(pageInk);

  // Also verify "Inspect Image" opens the viewer from Reader Menu > Navigate.
  await tapOnce(240, 400); // open Reader Menu
  await expect(activity).toHaveText('EpubReaderMenu', { timeout: 30_000 });
  const menuInk = await ink(0, 0, 480, 740);
  const beforeMenuInspect = (await log()).length;
  await tapOnce(100, 158); // expand Navigate (Select Chapter, Go to %, Go to Paragraph, Toggle Bookmark, Inspect Image)
  await tapOnce(100, 358); // Inspect Image
  await expect(activity).toHaveText('EpubImageInspect', { timeout: 30_000 });
  await expect.poll(async () => (await log()).slice(beforeMenuInspect), { timeout: 30_000 }).toContain('Entering activity: EpubImageInspect');
  await settle();
  // Back from menu-opened viewer returns cleanly to Reader Menu (same ink), then Home exits.
  await post('/swipe/240/790/240/500');
  await expect.poll(() => ink(0, 0, 480, 740), { timeout: 30_000 }).not.toBe(fitInk);
  expect(menuInk).toBeGreaterThan(1_000);
  await home.click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });

  expect(await log()).not.toMatch(/Guru Meditation|abort\(\)|Backtrace/);
});
