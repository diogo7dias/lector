import { test, expect } from 'e2e';

// The capacitive Home key goes Home from everywhere, the light panel included: it puts the
// panel away (its values saved as any close saves them) and leaves the screen under it. Before,
// the panel took every pass and never asked about Home, so the key did nothing until it was
// closed by hand. Checked over Settings and over an open book, since a book's own Home handling
// sits behind the same gate.
test('Home key closes the light panel and goes Home', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  const panel = screen.getByRole('img', { name: 'Device screen' });
  const home = screen.getByRole('button', { name: 'Home' });
  await expect(activity).not.toHaveText('', { timeout: 120_000 });
  if ((await activity.textContent()) !== 'Home') {
    await home.click(); // an earlier suite left a screen open
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
  }

  const settle = () => expect(busy).toHaveText('no', { timeout: 60_000 });
  const log = async () => (await fetch(`${app.baseUrl}/log`)).text();
  const tapOnce = async (x: number, y: number) => {
    await panel.tap({ position: { x, y } });
    await settle();
  };
  const ink = async (x: number, y: number, w: number, h: number) =>
    (await (await fetch(`${app.baseUrl}/ink/${x}/${y}/${w}/${h}`)).json()).ink as number;
  const state = async () => (await (await fetch(`${app.baseUrl}/state`)).json()) as { frames: number; ink: number };
  const homeFromPanel = async (over: string) => {
    const before = await state();
    // A pull down from the top edge opens the panel over any screen: a band of rows and buttons.
    expect((await fetch(`${app.baseUrl}/swipe/240/5/240/400`, { method: 'POST' })).ok).toBe(true);
    await settle();
    await expect.poll(async () => Math.abs((await state()).ink - before.ink), { message: 'the panel opened', timeout: 30_000 })
      .toBeGreaterThan(5_000);
    await expect(activity).toHaveText(over);

    await home.click();
    await settle();
    await expect(activity, `Home key over the panel on ${over}`).toHaveText('Home', { timeout: 30_000 });
  };

  await tapOnce(240, 729); // Settings
  await expect(activity).toHaveText('Settings', { timeout: 30_000 });
  await homeFromPanel('Settings');

  // The suite shares one card: the book may already be Home's first row (blank on a fresh card).
  const listed = (await ink(0, 230, 480, 60)) > 500;
  if (listed) {
    await tapOnce(240, 259);
  } else {
    await tapOnce(240, 517); // Browse Files
    await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
    await tapOnce(150, 248); // test_jpeg_images.epub
  }
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
  await settle();
  await homeFromPanel('EpubReader');

  if (!listed) {
    // Opening it filed the book into Recents. Back in, then the in-book menu's Book >
    // Remove from Recents leaves it unfiled at the card root, where later files look for it.
    await tapOnce(240, 259);
    await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
    await settle();
    expect((await fetch(`${app.baseUrl}/swipe/240/795/240/450`, { method: 'POST' })).ok).toBe(true);
    await settle();
    await expect(activity).toHaveText('EpubReaderMenu', { timeout: 30_000 });
    await tapOnce(100, 358); // Book (below expanded Navigate's 4 rows)
    await tapOnce(100, 318); // Remove from Recents
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
    expect(await ink(0, 230, 480, 60), 'removed from Home recents').toBeLessThan(500);
  }

  expect(await log()).not.toMatch(/Guru Meditation|abort\(\)|Backtrace/);
});
