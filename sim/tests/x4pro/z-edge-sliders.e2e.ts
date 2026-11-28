import { test, expect } from 'e2e';

// Vertical drags inside the outer 12% strips adjust brightness (left) and warmth (right)
// live without opening the Light Panel, flashing the screen, or turning the page, and
// persist once on lift.
test('outer-edge vertical drags adjust brightness and warmth without flashing', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const frames = screen.getByTestId('frames');
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

  if ((await ink(0, 200, 480, 60)) > 500) {
    await tapOnce(240, 259);
  } else {
    await tapOnce(240, 517);
    await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
    await tapOnce(150, 279);
  }
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
  await expect(busy).toHaveText('no', { timeout: 30_000 });

  const framesBefore = await frames.textContent();
  const countSaves = async () => {
    const log = await (await fetch(`${app.baseUrl}/log`)).text();
    return [...log.matchAll(/Edge slider saved: on=(\d+) brightness=(\d+) warmth=(\d+)/g)];
  };
  const beforeCount = (await countSaves()).length;

  // Drag up on left 12% edge (brightness): no panel flash, no page turn, saves once on lift.
  await fetch(`${app.baseUrl}/swipe/25/600/25/250?ms=600`, { method: 'POST' });
  await expect.poll(async () => (await countSaves()).length, { timeout: 15_000 }).toBe(beforeCount + 1);
  const afterLeft = (await countSaves()).at(-1)!;
  expect(Number(afterLeft[1]), 'brightness drag turned frontlight on').toBe(1);
  expect(Number(afterLeft[2]), 'brightness rose above 0').toBeGreaterThan(0);
  expect(await frames.textContent(), 'left edge drag did not flash or turn the page').toBe(framesBefore);
  expect(await activity.textContent()).toBe('EpubReader');

  // Drag up on right 12% edge (warmth): no panel flash, no page turn, saves once on lift.
  const warmthBefore = Number(afterLeft[3]);
  await fetch(`${app.baseUrl}/swipe/455/600/455/250?ms=600`, { method: 'POST' });
  await expect.poll(async () => (await countSaves()).length, { timeout: 15_000 }).toBe(beforeCount + 2);
  const afterRight = (await countSaves()).at(-1)!;
  expect(Number(afterRight[3]), 'warmth rose').toBeGreaterThan(warmthBefore);
  expect(await frames.textContent(), 'right edge drag did not flash or turn the page').toBe(framesBefore);
  expect(await activity.textContent()).toBe('EpubReader');

  await screen.getByRole('button', { name: 'Home' }).click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});
