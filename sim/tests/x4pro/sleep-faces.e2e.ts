import { test, expect } from 'e2e';

// The Sleep Screen picker offers nine faces. Dark, None and Quick Resume are drawn without an
// SD file, so their result is exact: ink on the whole panel, and Quick Resume leaves the page
// it was on (a moon added in the bottom-left corner) and wakes into the same page.
// One test per face: a lock and a wake each take about a minute in the emulator.

// Picker rows, top to bottom: Dark, Light, Custom, Cover, Cover + Custom, None, Quick Resume,
// Transparent, Cover + Transparent.
const rowY = { dark: 262, none: 457, quickResume: 496 };

async function device({ app, screen }: any) {
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
  await expect(activity).not.toHaveText('', { timeout: 120_000 });
  return {
    activity,
    busy,
    panel,
    tapTwice,
    ink: async (x: number, y: number, w: number, h: number) =>
      ((await (await fetch(`${app.baseUrl}/ink/${x}/${y}/${w}/${h}`)).json()) as { ink: number }).ink,
    panelInk: async () => Number(await screen.getByTestId('ink').textContent()),
    pick: async (face: keyof typeof rowY) => {
      if ((await activity.textContent()) !== 'Home') {
        await screen.getByRole('button', { name: 'Home' }).click();
        await expect(activity).toHaveText('Home', { timeout: 30_000 });
      }
      await tapTwice(240, 729); // Settings
      await expect(activity).toHaveText('Settings', { timeout: 30_000 });
      await tapTwice(100, 84); // Display
      await tapTwice(150, 233); // Sleep Screen
      await tapTwice(240, rowY[face]);
      await screen.getByRole('button', { name: 'Home' }).click();
      await expect(activity).toHaveText('Home', { timeout: 30_000 });
    },
    lock: async () => {
      await power.click();
      await expect(activity).toHaveText('Sleep', { timeout: 30_000 });
      await expect(asleep).toHaveText('yes', { timeout: 60_000 });
    },
    wake: async () => {
      await power.click();
      await expect(asleep).toHaveText('no', { timeout: 120_000 });
      // A wake goes back into the last book, not necessarily to the screen that was locked.
      await expect(activity).not.toHaveText('Sleep', { timeout: 60_000 });
      await expect(busy).toHaveText('no', { timeout: 60_000 });
    },
  };
}

test('Dark sleeps on a black page', async (ctx) => {
  const d = await device(ctx);
  await d.pick('dark');
  await d.lock();
  expect(await d.panelInk(), 'Dark: a black page with the crow').toBeGreaterThan(350_000);
  await d.wake();
});

test('None sleeps on an empty page', async (ctx) => {
  const d = await device(ctx);
  await d.pick('none');
  const frames = async () => Number(await ctx.screen.getByTestId('frames').textContent());
  const framesBefore = await frames();
  await d.lock();
  expect(await d.panelInk(), 'None: an empty page').toBe(0);
  // Popup + four pre-clear passes; the white page is the last pass, so no HALF repeats it.
  expect(await frames() - framesBefore, 'None: no second white flash').toBe(5);
  await d.wake();
});

test('Quick Resume keeps the page and wakes into it', async (ctx) => {
  const d = await device(ctx);
  const { screen } = ctx;
  await d.pick('quickResume');
  // The suite shares one card: an earlier test may already have filed the book into Recents
  // (Home then shows it as its first row) and left it on the contents page, where
  // touch-select.e2e.ts expects to find it again. A fresh open starts on the cover image:
  // turn one page to the contents, whose ink is exact.
  const resume = (await d.ink(0, 200, 480, 60)) > 500; // blank on a fresh card
  if (resume) {
    await d.tapTwice(240, 259); // the book's row on Home
  } else {
    await d.tapTwice(240, 517); // Browse Files
    await expect(d.activity).toHaveText('FileBrowser', { timeout: 30_000 });
    await d.tapTwice(150, 279); // test_kerning_ligature.epub
  }
  await expect(d.activity).toHaveText('EpubReader', { timeout: 60_000 });
  await expect(d.busy).toHaveText('no', { timeout: 30_000 });
  if (!resume) {
    const frames = screen.getByTestId('frames');
    const box = await d.panel.boundingBox();
    if (!box) throw new Error('no panel on the page');
    const framesBefore = await frames.textContent();
    await screen.swipe({ from: { x: box.x + 420, y: box.y + 420 }, to: { x: box.x + 100, y: box.y + 420 } });
    await expect(frames).not.toHaveText(framesBefore ?? '', { timeout: 30_000 });
    await expect(d.busy).toHaveText('no', { timeout: 60_000 });
  }
  // The moon is a 32 px tile in the bottom-left corner, over the status bar's page count.
  const moonBox: [number, number, number, number] = [6, 750, 50, 44];
  const bodyBox: [number, number, number, number] = [0, 60, 480, 680];
  const pageInk = await d.panelInk();
  const moonBefore = await d.ink(...moonBox);
  const bodyBefore = await d.ink(...bodyBox);
  await d.lock();
  expect(await d.ink(...moonBox), 'Quick Resume: the moon replaces the corner').not.toBe(moonBefore);
  // Not compared with bodyBefore: text anti-aliasing is on by default and the emulator does
  // not model gray, so before the lock the panel shows the gray planes. The moon's pass is
  // black and white, so what stays is the page's text: some ink, far from a full or empty page.
  const bodyLocked = await d.ink(...bodyBox);
  expect(bodyLocked, 'Quick Resume: the page stays').toBeGreaterThan(1000);
  expect(bodyLocked, 'Quick Resume: the page stays').toBeLessThan(100000);
  await d.wake();
  await expect(d.activity).toHaveText('EpubReader', { timeout: 60_000 });
  // The body only: the status bar's "+N" pages-this-session counter starts again at a wake.
  expect(await d.ink(...bodyBox), 'Quick Resume: the same page comes back').toBe(bodyBefore);
  expect(Math.abs((await d.panelInk()) - pageInk), 'Quick Resume: the moon is gone').toBeLessThan(200);
});
