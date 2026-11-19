import { test, expect } from 'e2e';

// Touch is the X4 Pro's main input: a first tap arms a row (dashed outline, PR #196),
// the second opens it, and the capacitive Home key comes back.
test('arms a row on the first tap, opens it on the second, Home comes back', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const frames = screen.getByTestId('frames');
  const busy = screen.getByTestId('busy');
  await expect(activity).toHaveText('Home', { timeout: 120_000 });

  const panel = screen.getByRole('img', { name: 'Device screen' });
  const before = await frames.textContent();
  await panel.tap({ position: { x: 240, y: 464 } }); // Browse Files
  await expect(frames).not.toHaveText(before ?? '', { timeout: 30_000 });
  await expect(busy).toHaveText('no', { timeout: 30_000 });
  await expect(activity).toHaveText('Home');

  // The armed row (x 20-459, y 442-486) carries a 2 px dashed border on every side:
  // each edge strip is partly inked, never solid and never blank.
  const edges = { top: [20, 442, 440, 2], bottom: [20, 485, 440, 2], left: [20, 442, 2, 45], right: [458, 442, 2, 45] };
  for (const [edge, [x, y, w, h]] of Object.entries(edges)) {
    const { ink, area } = await (await fetch(`${app.baseUrl}/ink/${x}/${y}/${w}/${h}`)).json();
    expect(ink / area, `${edge} edge is dashed`).toBeGreaterThan(0.3);
    expect(ink / area, `${edge} edge is dashed`).toBeLessThan(0.9);
  }

  await panel.tap({ position: { x: 240, y: 464 } });
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });

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
  await panel.tap({ position: { x: 240, y: 464 } }); // arm Browse Files
  await expect
    .poll(async () => (await sinceHome()).includes('Restoring normal CPU frequency'), {
      message: 'the tap restored full clock',
      timeout: 30_000,
    })
    .toBe(true);
  await panel.tap({ position: { x: 240, y: 464 } });
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });

  await screen.getByRole('button', { name: 'Home' }).click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});
