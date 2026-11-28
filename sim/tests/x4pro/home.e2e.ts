import { test, expect } from 'e2e';

// Touch is the X4 Pro's main input: one tap opens a row, and the capacitive Home key
// comes back.
test('one tap opens a row, Home comes back', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  await expect(activity).toHaveText('Home', { timeout: 120_000 });

  const panel = screen.getByRole('img', { name: 'Device screen' });
  const ink = async (x: number, y: number, w: number, h: number) =>
    (await (await fetch(`${app.baseUrl}/ink/${x}/${y}/${w}/${h}`)).json()).ink as number;
  // On X4 Pro with no selection and nothing scrolling, slots 2..4 of the hint band (y=760..800) stay blank.
  expect(await ink(125, 762, 350, 36), 'Home hides dead Select/Up/Down hints on X4 Pro').toBe(0);

  await panel.tap({ position: { x: 240, y: 517 } }); // Browse Files
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
  expect(await ink(125, 762, 350, 36), 'FileBrowser hides dead Open/Up/Down hints when nothing scrolls').toBe(0);

  await screen.getByRole('button', { name: 'Home' }).click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});

// Left alone, the X4 Pro drops its CPU to the crystal clock, which also retimes the flash
// and PSRAM bus. The next touch has to bring both back, and the card has to answer after
// it: the file browser reads the SD card to list its rows. Continues from Home above.
test('an idle Home drops to low power, and a tap after it still reads the card', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  await expect(activity).toHaveText('Home', { timeout: 120_000 });

  const sinceHome = async () => {
    const text = await (await fetch(`${app.baseUrl}/log`)).text();
    return text.slice(text.lastIndexOf('Entering activity: Home'));
  };
  await expect
    .poll(async () => (await sinceHome()).includes('Going to low-power mode'), {
      message: 'the idle Home went to low power',
      timeout: 60_000,
    })
    .toBe(true);

  const panel = screen.getByRole('img', { name: 'Device screen' });
  await panel.tap({ position: { x: 240, y: 517 } }); // arm Browse Files
  await expect
    .poll(async () => (await sinceHome()).includes('Restoring normal CPU frequency'), {
      message: 'the tap restored full clock',
      timeout: 30_000,
    })
    .toBe(true);
  await panel.tap({ position: { x: 240, y: 517 } });
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });

  await screen.getByRole('button', { name: 'Home' }).click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});
