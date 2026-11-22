import { test, expect } from 'e2e';

// One tap opens; an action that cannot be undone takes a second. Settings > System >
// Clear Reading Cache shows its answers as buttons with no band: the first tap on Clear
// only outlines it, the second clears.
test('Clear Reading Cache needs a confirming second tap, everything before it one tap', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  await expect(activity).toHaveText('Home', { timeout: 120_000 });
  const panel = screen.getByRole('img', { name: 'Device screen' });
  const tap = async (x: number, y: number) => {
    await panel.tap({ position: { x, y } });
    await expect(busy).toHaveText('no', { timeout: 30_000 });
  };
  const ink = async (x: number, y: number, w: number, h: number) =>
    (await (await fetch(`${app.baseUrl}/ink/${x}/${y}/${w}/${h}`)).json()).ink as number;

  await tap(240, 729); // Settings
  await tap(100, 173); // System
  await tap(100, 703); // Clear Reading Cache
  await expect(activity).toHaveText('ClearCache', { timeout: 30_000 });
  expect(await ink(0, 762, 480, 38), 'a screen with answer buttons draws no band').toBe(0);

  const clearButton = await ink(242, 712, 218, 48);
  await tap(350, 735);
  // Still the warning: the dashed outline added ink round Clear, and nothing was cleared.
  await expect.poll(() => ink(242, 712, 218, 48), { timeout: 30_000 }).toBeGreaterThan(clearButton);
  await tap(350, 735);
  await expect.poll(() => ink(0, 712, 480, 48), { timeout: 60_000 }).toBe(0);
  await expect.poll(() => ink(0, 760, 120, 40), { timeout: 30_000 }).toBeGreaterThan(0); // Back

  // The suite shares one device: leave it on Home for the next file.
  await screen.getByRole('button', { name: 'Home' }).click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});
