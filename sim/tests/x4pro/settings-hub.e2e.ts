import { test, expect } from 'e2e';

// Settings opens on five framed cards, each with a summary line, and System keeps its
// rare rows one level down in Maintenance; Back from there lands on the Maintenance row.
test('hub cards summarise, and Maintenance sits under System', async ({ app, screen }) => {
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

  await tapOnce(240, 729); // Settings
  await expect(activity).toHaveText('Settings', { timeout: 30_000 });
  // Each card: a 2 px frame down its left edge, and a summary line under the name.
  for (const top of [70, 144, 218, 292, 366]) {
    expect(await ink(20, top + 4, 2, 50), `card frame at ${top}`).toBe(100);
    expect(await ink(30, top + 32, 200, 22), `summary under the name at ${top}`).toBeGreaterThan(0);
  }

  await tapOnce(240, 323); // System
  expect(await ink(14, 70, 60, 26), 'a black section heading leads the list').toBeGreaterThan(1000);
  await tapOnce(420, 781); // Page Down
  await tapOnce(150, 733); // Maintenance
  // Five rows and nothing under them.
  expect(await ink(20, 70, 300, 150), 'the five maintenance rows').toBeGreaterThan(0);
  expect(await ink(0, 230, 480, 500), 'nothing under them').toBe(0);

  await tapOnce(60, 781); // Back: to System, on the page that holds Maintenance
  expect(await ink(20, 722, 200, 22), 'the Maintenance row is on screen again').toBeGreaterThan(0);
  expect(await ink(0, 230, 480, 500), 'the System rows are back').toBeGreaterThan(0);

  await screen.getByRole('button', { name: 'Home' }).click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});
