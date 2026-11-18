import { test, expect } from 'e2e';

// Hold Confirm on a book, pick Book Info (the first row for an EPUB), see its cover and a
// page of text, and come back to the list. Leaves the device on Home for the next file.
test('File actions opens Book Info for an EPUB and returns to the list', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  await expect(activity).toHaveText('Home', { timeout: 60_000 });

  const press = async (name: string) => {
    await screen.getByRole('button', { name }).click();
    await expect(busy).toHaveText('no', { timeout: 60_000 });
  };
  await press('Confirm');
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
  await press('Up'); // the last book in the card's root

  const frames = await screen.getByTestId('frames').textContent();
  // Over the 1 s hold in device time, which the emulator runs behind the host clock.
  const held = await fetch(`${app.baseUrl}/press/confirm?ms=3000`, { method: 'POST' });
  expect(held.ok).toBe(true);
  await expect(screen.getByTestId('frames')).not.toHaveText(frames ?? '', { timeout: 30_000 });
  await expect(busy).toHaveText('no', { timeout: 60_000 });

  await press('Confirm');
  await expect(activity).toHaveText('DictionaryDefinition', { timeout: 60_000 });
  await expect.poll(async () => Number(await screen.getByTestId('ink').textContent())).toBeGreaterThan(2_000);
  // The book's cover (mostly black) sits centred above the text on page 1.
  const cover = await (await fetch(`${app.baseUrl}/ink/160/95/160/230`)).json();
  expect(cover.ink, 'the cover is drawn above the text').toBeGreaterThan(cover.area / 2);

  // A popped screen logs no "Entering", so the activity label keeps the old name: the
  // log says when Book Info handed the screen back to the list.
  await press('Back');
  await expect
    .poll(async () => (await (await fetch(`${app.baseUrl}/log`)).text()).includes('Exiting activity: DictionaryDefinition'))
    .toBe(true);
  await press('Back');
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});
