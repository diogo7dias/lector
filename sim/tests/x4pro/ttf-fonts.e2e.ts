import { test, expect } from 'e2e';

// The X4 Pro reads .ttf/.otf families from the card's hidden /.fonts folder (sim/fixtures/sd/.fonts, hidden so the file browser rows stay put):
// "Lexend" is a Latin subset of Lexend Deca (SIL OFL 1.1, sim/fixtures/sd/.fonts/Lexend/OFL.txt),
// "Broken" is the same file cut short. A good family draws the Text Settings preview in its own
// glyphs; a corrupt one logs why it was refused and the page keeps the built-in ChareInk look.
// The device lives across the whole suite, so the test puts ChareInk back at the end.

// Font picker rows, top to bottom: ChareInk, Broken, Lexend (alphabetical after the built-in).
const row = { chareInk: 88, broken: 128, lexend: 168 };

test('a TTF family draws its own glyphs, a corrupt one falls back to ChareInk', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  await expect(activity).not.toHaveText('', { timeout: 120_000 });
  // The device lives across the suite: an earlier test may have left it on another screen.
  if ((await activity.textContent()) !== 'Home') {
    await screen.getByRole('button', { name: 'Home' }).click();
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
  }
  const panel = screen.getByRole('img', { name: 'Device screen' });
  const tapOnce = async (x: number, y: number) => {
    await panel.tap({ position: { x, y } });
    await expect(busy).toHaveText('no', { timeout: 30_000 });
  };
  // Ink of the preview text above the settings list.
  const preview = async () =>
    ((await (await fetch(`${app.baseUrl}/ink/0/60/480/340`)).json()) as { ink: number }).ink;
  const log = async () => await (await fetch(`${app.baseUrl}/log`)).text();
  const pick = async (y: number) => {
    await tapOnce(240, 413); // Font row
    await expect(activity).toHaveText('FontPicker', { timeout: 30_000 });
    await tapOnce(200, y);
    await expect(busy).toHaveText('no', { timeout: 60_000 });
  };

  await tapOnce(240, 729); // Settings
  await expect(activity).toHaveText('Settings', { timeout: 30_000 });
  await tapOnce(100, 114); // Reader
  await tapOnce(100, 84); // Text Settings
  await expect(activity).toHaveText('TextSettings', { timeout: 30_000 });
  // Re-pick ChareInk first: the preview of a just-opened screen differs by a few hundred pixels from
  // one redrawn after a font pick, so the reference is taken the same way as every later reading.
  await pick(row.chareInk);
  const chareInk = await preview();
  expect(chareInk, 'the built-in preview has text').toBeGreaterThan(10_000);

  try {
    await pick(row.lexend);
    await expect.poll(log, { timeout: 60_000 }).toMatch(/\[TTF\] Lexend 14 pt: 1 face\(s\), PSRAM free \d+ -> \d+ KB, internal free \d+ -> \d+ B/);
    expect(await preview(), 'Lexend glyphs differ from ChareInk').not.toBe(chareInk);

    await pick(row.broken);
    await expect.poll(log, { timeout: 60_000 }).toMatch(/\[ERR\] \[TTF\] .*Broken-Regular\.ttf refused: file is cut short/);
    expect(await preview(), 'the cut-short family draws as ChareInk').toBe(chareInk);
  } finally {
    await pick(row.chareInk);
  }
  expect(await preview(), 'ChareInk is back').toBe(chareInk);
  expect(await log()).not.toMatch(/Guru Meditation|abort\(\)|Backtrace/);
});
