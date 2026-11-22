import { test, expect } from 'e2e';

// Settings > System > Time Zone: the pick is the zone's own screen and it shows on the row.
// Put back to UTC at the end, because the suites after this one share the device. The activity name stays
// TimezonePicker when the picker closes: Settings resumes without a new "Entering activity" line.
test('a time zone picked on its screen shows on its row', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  const panel = screen.getByRole('img', { name: 'Device screen' });
  const tapOnce = async (x: number, y: number) => {
    await panel.tap({ position: { x, y } });
    await expect(busy).toHaveText('no', { timeout: 30_000 });
  };
  const press = async (name: 'Up' | 'Down', times: number) => {
    for (let i = 0; i < times; i++) {
      await screen.getByRole('button', { name }).click();
      await expect(busy).toHaveText('no', { timeout: 30_000 });
    }
  };
  const ink = async (x: number, y: number, w: number, h: number) =>
    (await (await fetch(`${app.baseUrl}/ink/${x}/${y}/${w}/${h}`)).json()).ink as number;
  // The value column of the Time Zone row in Settings > System: "UTC" sits there, while a long
  // zone name wraps onto the line below and leaves it empty.
  const value = () => ink(380, 573, 82, 22);
  const openSystem = async () => {
    await expect(activity).not.toHaveText('', { timeout: 120_000 });
    if ((await activity.textContent()) !== 'Home') {
      await screen.getByRole('button', { name: 'Home' }).click();
      await expect(activity).toHaveText('Home', { timeout: 30_000 });
    }
    await tapOnce(240, 729); // Settings
    await tapOnce(100, 173); // System
  };
  const openTimeZone = async () => {
    await tapOnce(100, 583); // Time Zone
    await expect(activity).toHaveText('TimezonePicker', { timeout: 30_000 });
  };

  await openSystem();
  expect(await value(), 'UTC is the default and fits the row').toBeGreaterThan(0);
  await openTimeZone(); // opens on UTC
  await press('Down', 2); // London / Dublin / Lisbon, then Berlin / Paris / Madrid / Rome
  await tapOnce(100, 700);
  await expect.poll(value, { timeout: 30_000 }).toBe(0);

  // Out of Settings and back in: the row is read from the saved setting, not from the screen.
  // Not a reboot: the device boots into its last book, and files it into Recents, which the
  // suites after this one do not expect.
  await screen.getByRole('button', { name: 'Home' }).click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
  await openSystem();
  expect(await value(), 'the zone stayed after Settings was left').toBe(0);
  await openTimeZone(); // opens on Berlin
  await press('Up', 2);
  await tapOnce(100, 608);
  await expect.poll(value, { timeout: 30_000 }).toBeGreaterThan(0);

  // Leave on Home, where the suites after this one expect to start.
  await screen.getByRole('button', { name: 'Home' }).click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});
