import { test, expect } from 'e2e';

// A screen that reboots on the way out to give its heap back (USB Drive, the Wi-Fi screens)
// lands where Back would have. In QEMU USB Drive cannot start and waits to be left: Back
// soft-resets the chip, and the boot rebuilds the File Transfer mode list on USB Drive, whose
// Back then rebuilds Settings on the File Transfer card. Named to run last: it reboots.
test('the reboot out of USB Drive lands on the mode list, and its Back on Settings', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  await expect(activity).not.toHaveText('', { timeout: 120_000 });
  if ((await activity.textContent()) !== 'Home') {
    await screen.getByRole('button', { name: 'Home' }).click();
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
  }
  const panel = screen.getByRole('img', { name: 'Device screen' });
  const tapOnce = async (x: number, y: number) => {
    await panel.tap({ position: { x, y } });
    await expect(busy).toHaveText('no', { timeout: 30_000 });
  };
  const log = async () => (await (await fetch(`${app.baseUrl}/log`)).text()) as string;
  const since = async (from: number, needle: string, timeout = 60_000) =>
    expect.poll(async () => (await log()).slice(from).includes(needle), { timeout }).toBe(true);

  await tapOnce(240, 729); // Settings
  await expect(activity).toHaveText('Settings', { timeout: 30_000 });
  await tapOnce(240, 397); // File Transfer
  await expect(activity).toHaveText('NetworkModeSelection', { timeout: 60_000 });
  await tapOnce(240, 405); // USB Drive, the fifth mode
  await expect(activity).toHaveText('UsbDrive', { timeout: 30_000 });
  // No USB host in QEMU: the drive fails to start and offers Back.
  await expect.poll(async () => (await log()).includes('Unable to start USB Drive'), { timeout: 60_000 }).toBe(true);

  let from = (await log()).length;
  await tapOnce(60, 780); // Back
  // A boot: the hardware probe runs again (the restart line itself may not leave USB in time).
  await since(from, 'Hardware detect');
  // The File Transfer opener (4) on its USB Drive row (4), then the list on that row.
  await since(from, 'Rebuilding opener 4 on row 4');
  await expect(activity, 'the reboot lands on the mode list').toHaveText('NetworkModeSelection', {
    timeout: 120_000,
  });
  expect((await log()).slice(from), 'not on Home').not.toContain('Entering activity: Home');

  from = (await log()).length;
  await tapOnce(60, 780); // Back out of the mode list
  // The Settings opener that opened File Transfer (0), rebuilt on its card.
  await since(from, 'Rebuilding opener 0 on row 0');
  await expect(activity, 'Back from the mode list lands on Settings').toHaveText('Settings', { timeout: 60_000 });
  await tapOnce(60, 780); // Back
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});
