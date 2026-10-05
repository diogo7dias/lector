import { test, expect } from 'e2e';
// X3: same flow as the X4 (shared binary, same button ladder), on the UC8253 panel.

// Open the last book in the card's root and turn a page.
test('opens a book and turns a page', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const frames = screen.getByTestId('frames');
  await expect(activity).toHaveText('Home', { timeout: 60_000 });

  await screen.getByRole('button', { name: 'Confirm' }).click();
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
  await screen.getByRole('button', { name: 'Up' }).click();
  await screen.getByRole('button', { name: 'Confirm' }).click();
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });

  const before = await frames.textContent();
  await screen.getByRole('button', { name: 'Right' }).click();
  await expect(frames).not.toHaveText(before ?? '', { timeout: 30_000 });
  // A page of text, not a blank or a cleared panel.
  await expect.poll(async () => Number(await screen.getByTestId('ink').textContent())).toBeGreaterThan(5_000);
});
