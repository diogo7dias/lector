import { test, expect } from 'e2e';

// Settings > Reader > Reader Menu Style > Toolbar: a tap on the page centre paints a sheet
// over the bottom of the page instead of opening the list menu. Its scrub row jumps through
// the book, a tile opens an existing screen, and a tap above the sheet closes it. The file sorts
// after touch-select: its Settings visit leaves Home with another row selected, which that
// test's "is the book listed" check does not expect.
test('the toolbar menu scrubs, opens a tile and closes', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  await expect(activity).not.toHaveText('', { timeout: 120_000 });
  if ((await activity.textContent()) !== 'Home') {
    await screen.getByRole('button', { name: 'Home' }).click(); // an earlier suite left a book open
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
  }

  const panel = screen.getByRole('img', { name: 'Device screen' });
  const tapTwice = async (x: number, y: number) => {
    for (let i = 0; i < 2; i++) {
      await panel.tap({ position: { x, y } });
      await expect(busy).toHaveText('no', { timeout: 30_000 });
    }
  };
  const tapOnce = async (x: number, y: number) => {
    await panel.tap({ position: { x, y } });
    await expect(busy).toHaveText('no', { timeout: 60_000 });
  };
  const ink = async (x: number, y: number, w: number, h: number) =>
    (await (await fetch(`${app.baseUrl}/ink/${x}/${y}/${w}/${h}`)).json()).ink as number;
  const setStyle = async () => {
    await tapTwice(240, 729); // Settings
    await expect(activity).toHaveText('Settings', { timeout: 30_000 });
    await tapTwice(100, 114); // Reader
    await tapTwice(150, 413); // Reader Menu Style: List <-> Toolbar
    await screen.getByRole('button', { name: 'Home' }).click();
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
  };

  // The suite shares one card: an earlier test may have filed the book into Recents (Home then
  // lists it as its first row). Asked before Settings, which leaves Home with another row selected.
  const resume = (await ink(0, 230, 480, 60)) > 5_000;
  await setStyle();
  if (resume) {
    await tapTwice(240, 260); // the book's row on Home
  } else {
    await tapTwice(240, 517); // Browse Files
    await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
    await tapTwice(150, 279); // test_kerning_ligature.epub
  }
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
  await expect(busy).toHaveText('no', { timeout: 30_000 });

  // The sheet's band, y 583-800, is mostly white around a thin scrub row; the page text that
  // fills it without the sheet is far denser.
  const band = () => ink(0, 590, 480, 210);
  const withoutSheet = await band();
  await tapOnce(240, 400); // the page centre
  expect(await activity.textContent(), 'a sheet, not the list menu').toBe('EpubReader');
  await expect.poll(band, { timeout: 30_000 }).toBeLessThan(withoutSheet / 2);

  // Tap the track: the book jumps and the sheet is back over the new page.
  const before = await ink(0, 0, 480, 580);
  await tapOnce(300, 653);
  await expect.poll(() => ink(0, 0, 480, 580), { timeout: 60_000 }).not.toBe(before);
  await expect.poll(band, { timeout: 30_000 }).toBeLessThan(withoutSheet / 2);

  // The Contents tile arms on the first tap and opens the chapter list on the second.
  await tapOnce(90, 757);
  expect(await activity.textContent(), 'the first tap only arms').toBe('EpubReader');
  await tapOnce(90, 757);
  await expect(activity).toHaveText('EpubReaderChapterSelection', { timeout: 30_000 });
  await panel.tap({ position: { x: 60, y: 780 } }); // Back
  await expect(busy).toHaveText('no', { timeout: 60_000 });

  // The reader is back with no sheet: the page centre opens it again, and a tap above the sheet
  // closes it, the page whole again.
  await tapOnce(240, 400);
  await expect.poll(band, { timeout: 30_000 }).toBeLessThan(withoutSheet / 2);
  await tapOnce(240, 300);
  await expect.poll(band, { timeout: 60_000 }).toBeGreaterThan(withoutSheet / 2);

  // More opens the tabbed menu (an entered screen, so the label changes).
  await tapOnce(240, 400);
  await tapTwice(390, 757);
  await expect(activity).toHaveText('EpubReaderMenu', { timeout: 30_000 });

  // Back to the list, so later suites find the default menu.
  await screen.getByRole('button', { name: 'Home' }).click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
  await setStyle();
});
