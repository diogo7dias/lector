import { test, expect } from 'e2e';

// Keys-only boards: a Power double click opens the quick panel (no frontlight rows), the
// keys walk its action grid, and Confirm fires the button under the cursor. Out of a book the
// grid is Refresh, Rotate / Continue Reading, Random Book / Search, Settings: from the Sort row,
// Down lands on Refresh, Right on Rotate, Left back on Refresh. Refresh is the button fired:
// it leaves Home and its selected row as they were, which every later file relies on.
test('Power double click opens the quick panel and Confirm fires a grid button', async ({ app, screen }) => {
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
  const log = async () => (await fetch(`${app.baseUrl}/log`)).text();
  const double = async () => {
    const response = await fetch(`${app.baseUrl}/double/power`, { method: 'POST' });
    expect(response.ok).toBe(true);
    await expect(busy).toHaveText('no', { timeout: 60_000 });
  };
  const opens = async (from: number) =>
    expect.poll(async () => (await log()).slice(from).includes('[PANEL] Open (6 actions)'), { timeout: 30_000 }).toBe(true);

  // The same double click puts it away again.
  let from = (await log()).length;
  await double();
  await opens(from);
  from = (await log()).length;
  await double();
  await expect.poll(async () => (await log()).slice(from).includes('[PANEL] Close'), { timeout: 30_000 }).toBe(true);
  await expect(activity).toHaveText('Home');

  from = (await log()).length;
  await double();
  await opens(from);
  const frames = Number(await screen.getByTestId('frames').textContent());
  await press('Down');
  await press('Right');
  await press('Left');
  // Every cursor move repaints the band.
  expect(Number(await screen.getByTestId('frames').textContent())).toBeGreaterThan(frames);
  await press('Confirm');
  await expect
    .poll(async () => (await log()).slice(from).includes('Manual screen refresh triggered'), { timeout: 30_000 })
    .toBe(true);
  expect((await log()).slice(from)).toContain('[PANEL] Close');
  await expect(activity).toHaveText('Home');
});

// The Power double click shows nowhere on screen, so keys-only Home lists the panel as a row:
// Quick Panel, just above Settings. Confirm opens it through the same action, Back puts it
// away. Starts and ends on Browse Files, where the file before left Home.
test('the Quick Panel row on Home opens the panel', async ({ app, screen }) => {
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
  const log = async () => (await fetch(`${app.baseUrl}/log`)).text();

  await press('Down', 4); // Library, OPDS Browser, Nearby Sync, Quick Panel
  const from = (await log()).length;
  await press('Confirm');
  await expect.poll(async () => (await log()).slice(from).includes('[PANEL] Open'), { timeout: 30_000 }).toBe(true);
  // Opened on the press: the release of that Confirm must not fire a grid button.
  expect((await log()).slice(from)).not.toContain('[PANEL] Close');
  await press('Back');
  await expect.poll(async () => (await log()).slice(from).includes('[PANEL] Close'), { timeout: 30_000 }).toBe(true);
  await expect(activity).toHaveText('Home');
  await press('Up', 4); // back on Browse Files for the next file
});
