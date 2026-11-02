import { test, expect } from 'e2e';

// The border is 2 px wide and starts at the top inset, 9 on this board: whether pixel row 9 is
// inked says exactly where the top edge is. Cancel keeps it, Save moves it through a reboot.
test('a calibrated edge survives a reboot, and Cancel leaves it alone', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  const panel = screen.getByRole('img', { name: 'Device screen' });
  const home = screen.getByRole('button', { name: 'Home' });
  const ink = screen.getByTestId('ink');
  const settle = () => expect(busy).toHaveText('no', { timeout: 30_000 });
  const tap = async (x: number, y: number) => {
    await panel.tap({ position: { x, y } });
    await settle();
  };
  const rowNineInked = async () => {
    const { ink, area } = await (await fetch(`${app.baseUrl}/ink/100/9/200/1`)).json();
    return ink === area;
  };
  const open = async () => {
    await expect(activity).not.toHaveText('', { timeout: 120_000 });
    if ((await activity.textContent()) !== 'Home') {
      await home.click();
      await expect(activity).toHaveText('Home', { timeout: 30_000 });
    }
    for (const [x, y] of [[240, 729], [100, 84], [100, 383]]) {
      await tap(x, y);
      await tap(x, y);
    }
    await expect(activity).toHaveText('ScreenCalibration', { timeout: 30_000 });
    await settle();
  };

  // `activity` only follows a screen being entered, so leaving is read off the panel.
  const leave = async (action: () => Promise<void>) => {
    const before = await ink.textContent();
    await action();
    await expect(ink).not.toHaveText(before ?? '', { timeout: 30_000 });
    await settle();
  };

  await open();
  expect(await rowNineInked(), 'the board default').toBe(true);
  await tap(431, 123); // +
  await expect.poll(rowNineInked, { timeout: 30_000 }).toBe(false);
  await leave(async () => tap(60, 782)); // Cancel
  await home.click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });

  await open();
  expect(await rowNineInked(), 'Cancel kept the old edge').toBe(true);
  await tap(431, 123); // +
  await expect.poll(rowNineInked, { timeout: 30_000 }).toBe(false);
  await leave(async () => {
    await tap(100, 739);
    await tap(100, 739); // Save
  });
  await home.click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
  await screen.getByRole('button', { name: 'Reboot' }).click();
  await expect(busy).toHaveText('no', { timeout: 180_000 });

  await open();
  expect(await rowNineInked(), 'Save survived the reboot').toBe(false);

  // The suites share one device: put the board's edges back.
  await tap(100, 695);
  await tap(100, 695); // Reset to Defaults
  await expect.poll(rowNineInked, { timeout: 30_000 }).toBe(true);
  await leave(async () => {
    await tap(100, 739);
    await tap(100, 739); // Save
  });
  await home.click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});
