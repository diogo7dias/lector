import { test, expect } from 'e2e';

// A setting changed by touch survives a power cycle (it lives in flash).
test('a toggled setting survives a reboot', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  const panel = screen.getByRole('img', { name: 'Device screen' });
  const tapTwice = async (x: number, y: number) => {
    for (let i = 0; i < 2; i++) {
      await panel.tap({ position: { x, y } });
      await expect(busy).toHaveText('no', { timeout: 30_000 });
    }
  };
  // The value column of "Show Favorite Badge" in Settings > Display: OFF or ON.
  const value = async () => (await (await fetch(`${app.baseUrl}/ink/400/403/76/22`)).json()).ink as number;
  const openDisplay = async () => {
    await expect(activity).not.toHaveText('', { timeout: 120_000 });
    if ((await activity.textContent()) !== 'Home') {
      await screen.getByRole('button', { name: 'Home' }).click();
      await expect(activity).toHaveText('Home', { timeout: 30_000 });
    }
    await tapTwice(240, 729); // Settings
    await expect(activity).toHaveText('Settings', { timeout: 30_000 });
    await tapTwice(100, 84); // Display
  };

  await openDisplay();
  const off = await value();
  await tapTwice(150, 413); // Show Favorite Badge
  await expect.poll(value, { timeout: 30_000 }).not.toBe(off);
  const on = await value();

  await screen.getByRole('button', { name: 'Home' }).click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
  await screen.getByRole('button', { name: 'Reboot' }).click();
  await expect(busy).toHaveText('no', { timeout: 180_000 });

  await openDisplay();
  expect(await value(), 'the toggle kept its new value').toBe(on);
});
