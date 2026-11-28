import { test, expect } from 'e2e';

// Quick Panel Night mode tile flips the page polarity in one tap (and back cleanly),
// and tapping the bottom status bar band cycles N/M -> N left -> Book % -> Hidden -> N/M.
test('Quick Panel Night mode tile and status bar tap cycle work in EPUB reader', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
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
  const state = async () => (await (await fetch(`${app.baseUrl}/state`)).json()) as { frames: number; ink: number };
  const log = async () => (await fetch(`${app.baseUrl}/log`)).text();

  // Open test_kerning_ligature.epub (from Home Recents if already filed, else Browse Files).
  const listed = (await ink(0, 200, 480, 60)) > 500;
  if (listed) {
    await tapOnce(240, 259);
  } else {
    await tapOnce(240, 517); // Browse Files
    await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
    await tapOnce(150, 279); // test_kerning_ligature.epub
  }
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
  await settle();

  // Advance off the cover page onto a text page with a visible status bar.
  const coverFrames = (await state()).frames;
  expect((await fetch(`${app.baseUrl}/swipe/420/400/60/400`, { method: 'POST' })).ok).toBe(true);
  await expect.poll(async () => (await state()).frames, { timeout: 30_000 }).toBeGreaterThan(coverFrames);
  await settle();

  const body = () => ink(0, 40, 480, 700);
  const topBar = () => ink(0, 0, 480, 22);
  const bottomRight = () => ink(320, 768, 160, 24);
  const pageSlot = () => ink(0, 768, 140, 24);

  const dayInk = await body();
  expect(dayInk, 'text page has light background').toBeLessThan(150_000);

  // Pull down Quick Panel and tap the Night mode tile (row 1 col 0: x=127, y=343).
  const toggleNightFromQuickPanel = async () => {
    const before = await state();
    expect((await fetch(`${app.baseUrl}/swipe/240/5/240/400`, { method: 'POST' })).ok).toBe(true);
    await settle();
    await expect
      .poll(async () => Math.abs((await state()).ink - before.ink), { message: 'panel opened', timeout: 30_000 })
      .toBeGreaterThan(5_000);
    await tapOnce(127, 343); // Night mode tile
  };

  await toggleNightFromQuickPanel();
  await expect.poll(body, { message: 'page inverted into night mode', timeout: 30_000 }).toBeGreaterThan(200_000);

  // Toggle back to day mode cleanly.
  await toggleNightFromQuickPanel();
  await expect.poll(body, { message: 'page returned to day mode', timeout: 30_000 }).toBe(dayInk);

  // Tap bottom status bar band 4 times: N/M -> N left -> Book % -> Hidden -> N/M.
  const slotFraction = await pageSlot();
  const topVisible = await topBar();
  const rightVisible = await bottomRight();
  expect(slotFraction, 'status bar starts visible with N/M').toBeGreaterThan(20);
  expect(slotFraction, 'status bar is on white paper, not solid black').toBeLessThan(2000);
  expect(rightVisible, 'right status bar cluster is visible').toBeGreaterThan(20);

  await tapOnce(80, 775); // -> N left
  await expect.poll(pageSlot, { message: 'changed from N/M to N left', timeout: 30_000 }).not.toBe(slotFraction);
  const slotLeft = await pageSlot();
  expect(slotLeft, 'N left still draws text').toBeGreaterThan(20);
  expect(await topBar(), 'top status bar stays visible on N left').toBe(topVisible);

  await tapOnce(80, 775); // -> Book %
  await expect.poll(pageSlot, { message: 'changed from N left to Book %', timeout: 30_000 }).not.toBe(slotLeft);
  const slotPct = await pageSlot();
  expect(slotPct, 'Book % still draws text').toBeGreaterThan(20);
  expect(await topBar(), 'top status bar stays visible on Book %').toBe(topVisible);

  await tapOnce(80, 775); // -> Hidden (status bar disappears, page reflows)
  await expect.poll(bottomRight, { message: 'status bar hidden', timeout: 30_000 }).not.toBe(rightVisible);

  await tapOnce(80, 780); // -> back to N/M
  await expect.poll(bottomRight, { message: 'status bar restored', timeout: 30_000 }).toBeGreaterThan(20);
  await expect.poll(topBar, { message: 'top bar restored', timeout: 30_000 }).toBe(topVisible);
  await expect.poll(pageSlot, { message: 'status bar restored to N/M', timeout: 30_000 }).toBeGreaterThan(20);
  expect(await pageSlot(), 'restored format is no longer Book %').not.toBe(slotPct);

  // Leave the book back on Home so subsequent suites start clean.
  await home.click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
  expect(await log()).not.toMatch(/Guru Meditation|abort\(\)|Backtrace/);
});
