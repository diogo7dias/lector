import { test, expect } from 'e2e';

// The built-in ChareInk comes in 12, 14 and 16 pt. This runs on the card's default selection
// (the built-in family, no SD font chosen): the Size picker offers the three sizes, and each pick
// redraws the Text Settings preview, the real page renderer, in a different size. The device lives
// across the whole suite, so the test puts 14 pt back and returns Home at the end.

// Size picker rows, top to bottom, once the picker is open over the Size row (y 443).
const row = { pt12: 379, pt14: 417, pt16: 457 };

test('ChareInk offers 12, 14 and 16 pt and each redraws the page at its own size', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  await expect(activity).not.toHaveText('', { timeout: 120_000 });
  if ((await activity.textContent()) !== 'Home') {
    await screen.getByRole('button', { name: 'Home' }).click();
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
  }
  const panel = screen.getByRole('img', { name: 'Device screen' });
  const tapTwice = async (x: number, y: number) => {
    for (let i = 0; i < 2; i++) {
      await panel.tap({ position: { x, y } });
      await expect(busy).toHaveText('no', { timeout: 30_000 });
    }
  };
  // Ink of the preview text above the settings list.
  const preview = async () =>
    ((await (await fetch(`${app.baseUrl}/ink/0/60/480/340`)).json()) as { ink: number }).ink;
  const log = async () => await (await fetch(`${app.baseUrl}/log`)).text();
  const tapOnce = async (x: number, y: number) => {
    await panel.tap({ position: { x, y } });
    await expect(busy).toHaveText('no', { timeout: 30_000 });
  };
  // A first tap on a row arms it and the second opens it; the picker's items act on one tap.
  const pick = async (y: number) => {
    await tapTwice(240, 443);
    await tapOnce(240, y);
  };

  await tapTwice(240, 729); // Settings
  await expect(activity).toHaveText('Settings', { timeout: 30_000 });
  await tapTwice(100, 114); // Reader
  await tapTwice(100, 84); // Text Settings
  await expect(activity).toHaveText('TextSettings', { timeout: 30_000 });

  const ink: Record<string, number> = {};
  try {
    for (const [pt, y] of [['12', row.pt12], ['16', row.pt16], ['14', row.pt14]] as const) {
      const before = await preview();
      await pick(y);
      // The page redraws a moment after the pick settles: wait for the ink to move.
      await expect.poll(preview, { timeout: 30_000, message: `${pt} pt redraw` }).not.toBe(before);
      ink[pt] = await preview();
      expect(ink[pt], `${pt} pt preview has text`).toBeGreaterThan(10_000);
    }
  } finally {
    await pick(row.pt14);
  }
  // Three different sizes draw three different pages.
  expect(new Set(Object.values(ink)).size, `preview ink ${JSON.stringify(ink)}`).toBe(3);
  expect(await preview(), '14 pt is back').toBe(ink['14']);
  // Later files start from Home.
  await screen.getByRole('button', { name: 'Home' }).click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
  expect(await log()).not.toMatch(/Guru Meditation|abort\(\)|Backtrace/);
});
