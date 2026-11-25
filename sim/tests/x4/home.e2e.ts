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

// A screen that is only waiting for a button must let the CPU clock drop. The cache sweep
// used to hold full clock from its warning to its result, though the sweep itself blocks
// the loop and never needed the hint. Leaves the device on Home for the next file.
test('the Clear Reading Cache warning left idle drops to low power', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  await expect(activity).toHaveText('Home', { timeout: 60_000 });

  const press = async (name: string, times = 1) => {
    for (let i = 0; i < times; i++) {
      await screen.getByRole('button', { name }).click();
      await expect(busy).toHaveText('no', { timeout: 60_000 });
    }
  };
  const sinceWarning = async () => {
    const text = await (await fetch(`${app.baseUrl}/log`)).text();
    const entered = text.lastIndexOf('Entering activity: ClearCache');
    return entered < 0 ? '' : text.slice(entered);
  };

  await press('Down', 5); // Settings, after Library, OPDS Browser, Nearby Sync and Quick Panel
  await press('Confirm');
  await expect(activity).toHaveText('Settings', { timeout: 30_000 });
  await press('Down', 3); // System
  await press('Confirm');
  await press('Down', 13); // Maintenance, the last row (no Time Zone rows: the X4 has no clock chip)
  await press('Confirm');
  await press('Down'); // Clear Reading Cache
  await press('Confirm');
  await expect(activity).toHaveText('ClearCache', { timeout: 30_000 });

  await expect
    .poll(async () => (await sinceWarning()).includes('Going to low-power mode'), {
      message: 'the idle warning went to low power',
      timeout: 60_000,
    })
    .toBe(true);

  // Back answers Cancel, so nothing is cleared. Settings resumes underneath without a new
  // "Entering activity" line: Home is the next name the log gives.
  await press('Back');
  await press('Back'); // Maintenance to System
  await press('Back'); // System to the Settings cards
  await press('Back');
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
  await press('Up', 5); // Home remembers its row: back to Browse Files
});
