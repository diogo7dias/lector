import { test, expect } from 'e2e';

// The power key puts the reader to sleep (sleep screen, then deep sleep) and wakes it.
test('power sleeps and wakes', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const asleep = screen.getByTestId('asleep');
  const power = screen.getByRole('button', { name: 'Power' });
  await expect(activity).not.toHaveText('', { timeout: 120_000 });

  await power.click();
  await expect(activity).toHaveText('Sleep', { timeout: 30_000 });
  await expect(asleep).toHaveText('yes', { timeout: 60_000 });

  await power.click();
  await expect(asleep).toHaveText('no', { timeout: 120_000 });
  await expect(activity).not.toHaveText('Sleep', { timeout: 60_000 });
});
