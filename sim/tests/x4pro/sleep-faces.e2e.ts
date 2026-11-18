import { test, expect } from 'e2e';

// The Sleep Screen picker offers nine faces. Dark, None and Quick Resume are drawn without a
// SD file, so their result is exact: ink on the whole panel, and Quick Resume leaves the page
// it was on (a moon added in the bottom-left corner) and wakes into the same page.
test('Dark, None and Quick Resume sleep and wake', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const asleep = screen.getByTestId('asleep');
  const busy = screen.getByTestId('busy');
  const power = screen.getByRole('button', { name: 'Power' });
  const panel = screen.getByRole('img', { name: 'Device screen' });
  const tapTwice = async (x: number, y: number) => {
    for (let i = 0; i < 2; i++) {
      await panel.tap({ position: { x, y } });
      await expect(busy).toHaveText('no', { timeout: 30_000 });
    }
  };
  const ink = async (x: number, y: number, w: number, h: number) =>
    ((await (await fetch(`${app.baseUrl}/ink/${x}/${y}/${w}/${h}`)).json()) as { ink: number }).ink;
  await expect(activity).not.toHaveText('', { timeout: 120_000 });

  // Picker rows, top to bottom: Dark, Light, Custom, Cover, Cover + Custom, None, Quick Resume,
  // Transparent, Cover + Transparent.
  const rowY = { dark: 262, none: 457, quickResume: 496 };
  const pick = async (face: keyof typeof rowY) => {
    if ((await activity.textContent()) !== 'Home') {
      await screen.getByRole('button', { name: 'Home' }).click();
      await expect(activity).toHaveText('Home', { timeout: 30_000 });
    }
    await tapTwice(240, 729); // Settings
    await expect(activity).toHaveText('Settings', { timeout: 30_000 });
    await tapTwice(100, 84); // Display
    await tapTwice(150, 503); // Sleep Screen
    await tapTwice(240, rowY[face]);
    await screen.getByRole('button', { name: 'Home' }).click();
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
  };
  const lock = async () => {
    await power.click();
    await expect(activity).toHaveText('Sleep', { timeout: 30_000 });
    await expect(asleep).toHaveText('yes', { timeout: 60_000 });
  };
  const wake = async () => {
    await power.click();
    await expect(asleep).toHaveText('no', { timeout: 120_000 });
    // A wake goes back into the last book, not necessarily to the screen that was locked.
    await expect(activity).not.toHaveText('Sleep', { timeout: 60_000 });
    await expect(busy).toHaveText('no', { timeout: 60_000 });
  };
  const panelInk = async () => Number(await screen.getByTestId('ink').textContent());

  await pick('dark');
  await lock();
  expect(await panelInk(), 'Dark: a black page with the crow').toBeGreaterThan(350_000);
  await wake();

  await pick('none');
  await lock();
  expect(await panelInk(), 'None: an empty page').toBe(0);
  await wake();

  // Quick Resume locks a book page: the page stays with a moon in the corner, and the wake
  // puts the same page back.
  await pick('quickResume');
  await tapTwice(240, 517); // Browse Files
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
  await tapTwice(150, 279); // test_kerning_ligature.epub
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
  await expect(busy).toHaveText('no', { timeout: 30_000 });
  // The first page is a cover image; turn to a page of text, whose ink is exact.
  const frames = screen.getByTestId('frames');
  const box = await panel.boundingBox();
  if (!box) throw new Error('no panel on the page');
  const framesBefore = await frames.textContent();
  await screen.swipe({ from: { x: box.x + 420, y: box.y + 420 }, to: { x: box.x + 100, y: box.y + 420 } });
  await expect(frames).not.toHaveText(framesBefore ?? '', { timeout: 30_000 });
  await expect(busy).toHaveText('no', { timeout: 60_000 });
  // The moon is a 32 px tile in the bottom-left corner, over the status bar's page count.
  const moonBox: [number, number, number, number] = [6, 750, 50, 44];
  const bodyBox: [number, number, number, number] = [0, 60, 480, 680];
  const pageInk = await panelInk();
  const moonBefore = await ink(...moonBox);
  const bodyBefore = await ink(...bodyBox);
  await lock();
  expect(await ink(...moonBox), 'Quick Resume: the moon replaces the corner').not.toBe(moonBefore);
  expect(await ink(...bodyBox), 'Quick Resume: the page stays').toBe(bodyBefore);
  await wake();
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
  // The body only: the status bar's "+N" pages-this-session counter starts again at a wake.
  expect(await ink(...bodyBox), 'Quick Resume: the same page comes back').toBe(bodyBefore);
  expect(Math.abs((await panelInk()) - pageInk), 'Quick Resume: the moon is gone').toBeLessThan(200);
});
