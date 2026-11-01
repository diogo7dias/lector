import { test, expect } from 'e2e';

// Bound to the Home key from the Buttons screen: firing it freezes the screen against
// touch and keys, and only the same gesture lets go.
test('Quick Lock ignores input until its own gesture fires again', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const frames = screen.getByTestId('frames');
  const busy = screen.getByTestId('busy');
  const panel = screen.getByRole('img', { name: 'Device screen' });
  const home = screen.getByRole('button', { name: 'Home' });
  const tapTwice = async (x: number, y: number) => {
    for (let i = 0; i < 2; i++) {
      await panel.tap({ position: { x, y } });
      await expect(busy).toHaveText('no', { timeout: 30_000 });
    }
  };
  await expect(activity).toHaveText('Home', { timeout: 120_000 });
  await tapTwice(240, 729); // Settings
  await tapTwice(100, 143); // Controls
  await tapTwice(100, 203); // Buttons
  await expect(activity).toHaveText('ButtonBindings', { timeout: 30_000 });
  await tapTwice(100, 168); // Home Button
  await tapTwice(100, 288); // Outside Book, Single Click
  await panel.tap({ position: { x: 100, y: 565 } }); // Quick Lock, in the action list
  await expect(busy).toHaveText('no', { timeout: 30_000 });

  const before = await frames.textContent();
  await home.click();
  await expect(frames).not.toHaveText(before ?? '', { timeout: 30_000 });
  await expect(busy).toHaveText('no', { timeout: 30_000 });
  const locked = await frames.textContent();
  // The Locked badge, in the top right corner.
  const badge = await (await fetch(`${app.baseUrl}/ink/400/10/70/25`)).json();
  expect(badge.ink / badge.area).toBeGreaterThan(0.4);

  await tapTwice(100, 368);
  await screen.getByRole('button', { name: 'Up' }).click();
  await panel.tap({ position: { x: 100, y: 128 } });
  await expect(busy).toHaveText('no', { timeout: 30_000 });
  await expect(frames, 'nothing repaints while locked').toHaveText(locked ?? '');

  await home.click();
  await expect(frames).not.toHaveText(locked ?? '', { timeout: 30_000 });
  await expect(busy).toHaveText('no', { timeout: 30_000 });
  const unlocked = await frames.textContent();
  await panel.tap({ position: { x: 100, y: 368 } });
  await expect(frames, 'input answers again').not.toHaveText(unlocked ?? '', { timeout: 30_000 });
  await expect(busy).toHaveText('no', { timeout: 30_000 });

  // The suites share one device: give the Home key back, and leave it on Home.
  await tapTwice(100, 288);
  await panel.tap({ position: { x: 100, y: 465 } }); // Home, in the action list
  await expect(busy).toHaveText('no', { timeout: 30_000 });
  await home.click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});
