import { test, expect } from 'e2e';

// In night mode, images use Bayer dithering (never crushed to solid blobs) and follow
// Settings > Reader > Night Mode Images: Normal Polarity (default) keeps photos natural,
// while Invert flips them with the page (for white-background diagrams).
test('night mode images dither cleanly and switch between Normal Polarity and Invert', async ({ app, screen }) => {
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
  const ink = async (x: number, y: number, w: number, h: number) =>
    (await (await fetch(`${app.baseUrl}/ink/${x}/${y}/${w}/${h}`)).json()).ink as number;
  const log = async () => (await fetch(`${app.baseUrl}/log`)).text();

  // Turn on Night mode (and optionally toggle Night Mode Images) in Settings > Reader.
  const toggleReaderLookRow = async (y: number) => {
    await tapOnce(240, 729); // Settings
    await expect(activity).toHaveText('Settings', { timeout: 30_000 });
    await tapOnce(240, 175); // Reader
    await tapOnce(150, y);
    await home.click();
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
  };

  // Open test_jpeg_images.epub ("JPEG Image Tests", always row 0 in Library regardless of
  // whether it is at card root or /recents, and regardless of how many books sit on Home).
  const openJpegBook = async () => {
    await tapOnce(240, 570); // Library
    await expect(activity).toHaveText('Library', { timeout: 30_000 });
    await expect.poll(() => ink(0, 60, 480, 90), { timeout: 60_000 }).toBeGreaterThan(500);
    await settle();
    await tapOnce(240, 128); // JPEG Image Tests (test_jpeg_images.epub)
    await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
    await settle();
  };

  await openJpegBook();

  const box = await panel.boundingBox();
  if (!box) throw new Error('no panel on the page');
  // Ensure we are on chapter 2 (jpeg_format.jpg, 350x250 centered at x=65..415, y=167..417).
  // Solo runs start on chapter 1 (intro text, low ink); after image-filing the book resumes on ch 2.
  const imageArea = 280 * 180;
  const imageInk = () => ink(100, 200, 280, 180);
  if ((await imageInk()) < 15_000) {
    const beforeSwipe = await frames.textContent();
    await screen.swipe({ from: { x: box.x + 420, y: box.y + 420 }, to: { x: box.x + 100, y: box.y + 420 } });
    await expect(frames).not.toHaveText(beforeSwipe ?? '', { timeout: 30_000 });
    await settle();
  }

  // Return Home and enable Night mode (Normal Polarity is the default).
  await home.click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
  await toggleReaderLookRow(493); // Night mode: OFF -> ON

  // Reopen test_jpeg_images.epub via Library on the same image page.
  await openJpegBook();

  // Page margin outside image is black (night mode active).
  expect(await ink(0, 0, 480, 30), 'night mode active on page margin').toBeGreaterThan(10_000);
  const normalNightInk = await imageInk();
  // jpeg_format.jpg is ~91% white with ~7% dark lines/details. Under Normal Polarity with
  // Bayer dithering, it stays bright (< 35% dark) and preserves detail (> 2% dark, not solid white).
  expect(normalNightInk, 'Normal Polarity keeps bright photo detail').toBeGreaterThan(imageArea * 0.02);
  expect(normalNightInk, 'Normal Polarity keeps bright photo polarity (not inverted)').toBeLessThan(imageArea * 0.35);

  // Switch Night Mode Images to Invert (Settings > Reader > Night Mode Images at y=560).
  await home.click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
  await toggleReaderLookRow(560); // Normal Polarity -> Invert

  await openJpegBook();

  const invertNightInk = await imageInk();
  // Normal Polarity and Invert are exact bitwise complements inside the image rectangle.
  expect(
    Math.abs(normalNightInk + invertNightInk - imageArea),
    `Normal (${normalNightInk}) and Invert (${invertNightInk}) are exact complements (area=${imageArea})`
  ).toBeLessThan(imageArea * 0.02);
  expect(invertNightInk, 'Invert flips bright image to dark').toBeGreaterThan(imageArea * 0.65);

  // Clean up: restore settings (Night Mode Images -> Normal Polarity, Night mode -> OFF)
  // and remove test_jpeg_images.epub from Recents back to card root.
  await panel.tap({ position: { x: 240, y: 400 } });
  await expect(activity).toHaveText('EpubReaderMenu', { timeout: 30_000 });
  await tapOnce(100, 398); // Book (below expanded Navigate's 5 rows)
  await tapOnce(100, 318); // Remove from Recents
  await expect(activity).toHaveText('Home', { timeout: 30_000 });

  await toggleReaderLookRow(560); // Invert -> Normal Polarity
  await toggleReaderLookRow(493); // Night mode: ON -> OFF
  expect(await log()).not.toMatch(/Guru Meditation|abort\(\)|Backtrace/);
});
