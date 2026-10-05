import { test, expect } from 'e2e';

// Boot to Home, open the file browser, walk its rows and come back.
test('boots to Home and browses the SD card', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  await expect(activity).toHaveText('Home', { timeout: 60_000 });

  await screen.getByRole('button', { name: 'Confirm' }).click();
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });

  const frames = Number(await screen.getByTestId('frames').textContent());
  await screen.getByRole('button', { name: 'Down' }).click();
  await expect(screen.getByTestId('frames')).not.toHaveText(String(frames), { timeout: 30_000 });

  await screen.getByRole('button', { name: 'Back' }).click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});
