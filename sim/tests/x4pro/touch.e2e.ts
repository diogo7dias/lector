import { test, expect } from 'e2e';

// A tap that lifts before the firmware polls again still lands: the GT911 holds a frame
// until the firmware clears it, so the press is read and then the lift. Dropping the lift
// left a contact that never ended (the settings test failed on slow CI hosts).
test('a tap that lifts before the next poll still lands', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  await expect(activity).not.toHaveText('', { timeout: 120_000 });
  if ((await activity.textContent()) !== 'Home') {
    await screen.getByRole('button', { name: 'Home' }).click();
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
  }
  // ms=0: finger down and up back to back, with no poll in between.
  const tap = (x: number, y: number) => fetch(`${app.baseUrl}/tap/${x}/${y}?ms=0`, { method: 'POST' });
  await tap(240, 729); // arms Settings
  await tap(240, 729); // opens it
  await expect(activity).toHaveText('Settings', { timeout: 30_000 });
});
