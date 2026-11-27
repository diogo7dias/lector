import { test, expect } from 'e2e';

// A scroll swipe on a list must not leave the row it started on outlined. The finger lands on a
// row (the pressed highlight is the dashed outline), drags a screen's worth, and lifts. The drag
// fires the list scroll and swallows the rest of the contact, so the lift never reached the UI
// and the outline stayed on that row after the page moved, through every repaint after it.
test('a slow scroll swipe leaves no row outlined', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  const panel = screen.getByRole('img', { name: 'Device screen' });
  await expect(activity).not.toHaveText('', { timeout: 120_000 });
  if ((await activity.textContent()) !== 'Home') {
    await screen.getByRole('button', { name: 'Home' }).click(); // an earlier suite left a screen open
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
  }
  const settle = () => expect(busy).toHaveText('no', { timeout: 60_000 });
  const tapOnce = async (x: number, y: number) => {
    await panel.tap({ position: { x, y } });
    await settle();
  };
  const ink = async (x: number, y: number, w: number, h: number) =>
    (await (await fetch(`${app.baseUrl}/ink/${x}/${y}/${w}/${h}`)).json()).ink as number;

  await tapOnce(240, 729); // Settings
  await tapOnce(240, 320); // System: two screens of rows
  await expect(activity).toHaveText('Settings', { timeout: 30_000 });
  const firstScreen = await ink(0, 60, 480, 690);

  // Down on Use Book Metadata, 1.5 s up 100 px (a slow drag, not the SDK flick), lift.
  expect((await fetch(`${app.baseUrl}/swipe/240/203/240/103?ms=1500`, { method: 'POST' })).ok).toBe(true);
  await settle();
  await expect.poll(() => ink(0, 60, 480, 690), { message: 'the list scrolled', timeout: 30_000 })
    .not.toBe(firstScreen);
  // The second screen puts Use Book Metadata at y 160-188; the outline's left side runs down x 0-5.
  expect(await ink(0, 158, 6, 32), 'no outline on the row the swipe started on').toBeLessThan(20);

  await screen.getByRole('button', { name: 'Home' }).click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});
