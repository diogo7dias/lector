import { test, expect } from 'e2e';

// On the keys: Confirm on a number in Text Settings docks the adjuster under the preview,
// Left and Right step it, Back puts it away, and the screen never changes.
test('a Text Settings number steps in place by the keys', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  await expect(activity).not.toHaveText('', { timeout: 60_000 });
  const press = async (name: string, times = 1) => {
    for (let i = 0; i < times; i++) {
      await screen.getByRole('button', { name }).click();
      await expect(busy).toHaveText('no', { timeout: 60_000 });
    }
  };
  const ink = async (x: number, y: number, w: number, h: number) =>
    (await (await fetch(`${app.baseUrl}/ink/${x}/${y}/${w}/${h}`)).json()).ink as number;
  const stepper = () => ink(352, 440, 36, 34); // the strip's boxed minus
  const value = () => ink(380, 410, 82, 22); // the number on the strip's name line

  // An earlier suite may have left a book open. Its Back returns where it was opened: Home on
  // its first row, or the browser, whose Back lands Home on Browse Files, five rows above Settings.
  let viaBrowser = false;
  while ((await activity.textContent()) !== 'Home') {
    viaBrowser ||= (await activity.textContent()) === 'FileBrowser';
    const leaving = await activity.textContent();
    await press('Back');
    await expect(activity).not.toHaveText(leaving ?? '', { timeout: 30_000 });
  }
  await press(viaBrowser ? 'Down' : 'Up', viaBrowser ? 5 : 1); // Up from the first row wraps to Settings
  await press('Confirm');
  await expect(activity).toHaveText('Settings', { timeout: 30_000 });
  await press('Down'); // Reader
  await press('Confirm');
  await press('Confirm'); // Text Settings, the first row under the TEXT heading
  await expect(activity).toHaveText('TextSettings', { timeout: 30_000 });

  await press('Down', 4); // Line Spacing, after Font, Size, Paperback Look and Ink Weight
  await press('Confirm');
  await expect.poll(stepper, { timeout: 30_000 }).toBeGreaterThan(0);
  const before = await value();
  await press('Right', 2);
  await expect.poll(value, { timeout: 30_000 }).not.toBe(before);
  await press('Left', 2);
  await expect.poll(value, { timeout: 30_000 }).toBe(before);
  await press('Back'); // Done
  await expect.poll(stepper, { timeout: 30_000 }).toBe(0);
  await expect(activity, 'the strip never left the screen').toHaveText('TextSettings');

  await press('Back'); // Text Settings to Reader
  await press('Back'); // Reader to the Settings cards
  await press('Back'); // to Home
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
  await press('Down'); // Home remembers Settings; Down wraps to the first row
});
