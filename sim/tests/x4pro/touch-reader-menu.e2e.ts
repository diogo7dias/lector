import { test, expect } from 'e2e';

// The in-book menu's Night mode, Frontlight and Go Home rows: Night mode inverts the page
// when the menu closes and survives a reboot, Frontlight flips its value in place, and Go
// Home leaves the book for Home.
test('night mode, frontlight and go home from the in-book menu', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  await expect(activity).not.toHaveText('', { timeout: 120_000 });
  if ((await activity.textContent()) !== 'Home') {
    await screen.getByRole('button', { name: 'Home' }).click(); // an earlier suite left a book open
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
  }

  const panel = screen.getByRole('img', { name: 'Device screen' });
  const tapOnce = async (x: number, y: number) => {
    await panel.tap({ position: { x, y } });
    await expect(busy).toHaveText('no', { timeout: 30_000 });
  };
  const ink = async (x: number, y: number, w: number, h: number) =>
    (await (await fetch(`${app.baseUrl}/ink/${x}/${y}/${w}/${h}`)).json()).ink;
  // The page body above the status bar: its "+N" counts pages turned this session and
  // starts again after a reboot.
  const body = () => ink(0, 0, 480, 740);

  // The suite shares one card: an earlier test may have filed the book into Recents (Home
  // then lists it as its first row, blank on a fresh card).
  if ((await ink(0, 200, 480, 60)) > 500) { // blank on a fresh card
    await tapOnce(240, 259); // the book's row on Home
  } else {
    await tapOnce(240, 517); // Browse Files
    await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
    await tapOnce(150, 279); // test_kerning_ligature.epub
  }
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
  await expect(busy).toHaveText('no', { timeout: 30_000 });

  // The menu opens on its first heading; Look is the one above it. With Look open its last
  // rows are Night mode (y 623) and Frontlight (y 663). Back first closes the tab, then the menu.
  const openLook = async () => {
    await panel.tap({ position: { x: 240, y: 400 } });
    await expect(activity).toHaveText('EpubReaderMenu', { timeout: 30_000 });
    await tapOnce(100, 143);
  };
  const closeMenu = async () => {
    for (let i = 0; i < 2; i++) {
      await panel.tap({ position: { x: 60, y: 780 } });
      await expect(busy).toHaveText('no', { timeout: 30_000 });
    }
  };

  const day = await body();
  await openLook();
  const lightOff = await ink(380, 650, 100, 30);
  // The row just tapped is the highlighted one, so each value is compared on a row that is not.
  await tapOnce(100, 663);
  await tapOnce(100, 623);
  expect(await ink(380, 650, 100, 30), 'the Frontlight value changed in place').not.toBe(lightOff);
  await closeMenu();
  await expect.poll(async () => (await body()) === day, { timeout: 30_000 }).toBe(false);
  const night = await body();

  // Night mode is a saved setting: the book comes back inverted after a power cycle.
  await screen.getByRole('button', { name: 'Reboot' }).click();
  await expect(busy).toHaveText('no', { timeout: 180_000 });
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
  await expect.poll(body, { timeout: 60_000 }).toBe(night);

  // Both back off: the same page as before.
  await openLook();
  await tapOnce(100, 663);
  await tapOnce(100, 623);
  expect(await ink(380, 650, 100, 30), 'Frontlight reads OFF again').toBe(lightOff);
  await closeMenu();
  await expect.poll(body, { timeout: 30_000 }).toBe(day);

  // Navigate opens expanded; Back collapses it so Device sits at y 263 regardless of how
  // many Navigate rows this book has, and expanding Device places Go Home at y 543.
  await panel.tap({ position: { x: 240, y: 400 } });
  await expect(activity).toHaveText('EpubReaderMenu', { timeout: 30_000 });
  await tapOnce(60, 780);
  await tapOnce(100, 263);
  await tapOnce(100, 543);
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});

// Back from a screen a reader menu row opened lands on the menu again, by a tap on the Back
// hint: Reader Settings (Text Settings) and Customise Status Bar. A screen that comes back is
// entered again, so the label turns back to the menu; a pop to the page would keep the old name.
test('Back from a reader menu row lands on the menu', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  await expect(activity).toHaveText('Home', { timeout: 120_000 });
  const panel = screen.getByRole('img', { name: 'Device screen' });
  const tapOnce = async (x: number, y: number) => {
    await panel.tap({ position: { x, y } });
    await expect(busy).toHaveText('no', { timeout: 30_000 });
  };
  const ink = async (x: number, y: number, w: number, h: number) =>
    (await (await fetch(`${app.baseUrl}/ink/${x}/${y}/${w}/${h}`)).json()).ink;

  if ((await ink(0, 200, 480, 60)) > 500) {
    await tapOnce(240, 259); // the book's row on Home
  } else {
    await tapOnce(240, 517); // Browse Files
    await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
    await tapOnce(150, 279); // test_kerning_ligature.epub
  }
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
  await expect(busy).toHaveText('no', { timeout: 30_000 });

  await tapOnce(240, 400);
  await expect(activity).toHaveText('EpubReaderMenu', { timeout: 30_000 });
  await tapOnce(100, 143); // Look
  // Its rows from the top: Reader Settings (y 183), Status Bar, Customise Status Bar (y 263).
  for (const [y, name] of [
    [183, 'TextSettings'],
    [263, 'StatusBarSettings'],
  ] as const) {
    await tapOnce(100, y);
    await expect(activity).toHaveText(name, { timeout: 30_000 });
    await tapOnce(60, 780); // Back
    await expect(activity, `Back from ${name} lands on the reader menu`).toHaveText('EpubReaderMenu', {
      timeout: 30_000,
    });
  }

  await screen.getByRole('button', { name: 'Home' }).click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});
