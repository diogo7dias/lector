import { test, expect } from 'e2e';
// X3: same flow as the X4 (shared binary, same button ladder). Runs on both X3 panels:
// the UC8253 and, as LECTOR_BOARD=x3uc8279, the UC8279d the boot probe must pick out.

// Boot to Home, open the file browser, walk its rows and come back.
test('boots to Home and browses the SD card', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  await expect(activity).toHaveText('Home', { timeout: 60_000 });
  const controller = process.env.LECTOR_BOARD === 'x3uc8279' ? 'UC8279' : 'UC8253';
  await expect(screen.getByTestId('controller')).toHaveText(controller);

  await screen.getByRole('button', { name: 'Confirm' }).click();
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });

  const frames = Number(await screen.getByTestId('frames').textContent());
  await screen.getByRole('button', { name: 'Down' }).click();
  await expect(screen.getByTestId('frames')).not.toHaveText(String(frames), { timeout: 30_000 });

  await screen.getByRole('button', { name: 'Back' }).click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});
