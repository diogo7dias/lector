import { test, expect } from 'e2e';

// A number in Text Settings is adjusted on the same screen: the row docks a strip under the
// live preview, the plus moves the value and the preview reflows, and Done puts it away.
test('line spacing adjusts in place under the preview', async ({ app, screen }) => {
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
  const ink = async (x: number, y: number, w: number, h: number) =>
    (await (await fetch(`${app.baseUrl}/ink/${x}/${y}/${w}/${h}`)).json()).ink as number;
  const stepper = () => ink(352, 440, 36, 34); // the strip's boxed minus
  // The preview's last lines: a taller line spacing moves them, though the ink count of the
  // whole pane can stay the same.
  const preview = () => ink(0, 330, 480, 60);
  const value = () => ink(380, 410, 82, 22); // the number on the strip's name line

  await tapOnce(240, 729); // Settings
  await tapOnce(240, 175); // Reader
  await tapOnce(100, 113); // Text Settings
  await expect(activity).toHaveText('TextSettings', { timeout: 30_000 });
  expect(await stepper(), 'no strip before a number is opened').toBe(0);

  await tapOnce(150, 533); // Line Spacing
  await expect.poll(stepper, { timeout: 30_000 }).toBeGreaterThan(0);
  const before = { value: await value(), preview: await preview() };
  await tapOnce(432, 456); // plus
  await tapOnce(432, 456);
  await tapOnce(432, 456);
  await expect.poll(value, { timeout: 30_000 }).not.toBe(before.value);
  expect(await preview(), 'the preview reflowed').not.toBe(before.preview);
  await expect(activity, 'no dialog was pushed').toHaveText('TextSettings');

  // Put it back for the suites after this one, then close the strip.
  await tapOnce(370, 456); // minus
  await tapOnce(370, 456);
  await tapOnce(370, 456);
  await expect.poll(value, { timeout: 30_000 }).toBe(before.value);
  await tapOnce(60, 781); // Done
  await expect.poll(stepper, { timeout: 30_000 }).toBe(0);
  await expect(activity).toHaveText('TextSettings');

  await screen.getByRole('button', { name: 'Home' }).click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});
