import { test, expect } from 'e2e';

// Open a Markdown file from /read on X4 Pro (touch), verify rendered heading + body and page
// turn, open the Book Menu popup, enter Text Settings, and verify Back returns to the page.
// Named zzz- to run last on the board, leaving Home clean afterwards.
test('opens a Markdown file by touch, turns a page, enters Text Settings, and returns via Back', async ({
  app,
  screen,
}) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  const frames = screen.getByTestId('frames');
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
  // Page body as 20 px ink bands above the status bar (which tracks "+N" session pages and clock).
  const body = async () => {
    const bands: number[] = [];
    for (let y = 0; y < 740; y += 20) {
      bands.push((await (await fetch(`${app.baseUrl}/ink/0/${y}/480/20`)).json()).ink);
    }
    return bands;
  };

  await tapOnce(240, 517); // Browse Files
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
  await tapOnce(80, 124); // [read]
  await tapOnce(120, 155); // z_markdown_sample.md (row 2 after Search and [pics])
  await expect(activity).toHaveText('MarkdownReader', { timeout: 60_000 });
  await expect(busy).toHaveText('no', { timeout: 30_000 });

  // Heading and formatted content draw on page 1.
  await expect
    .poll(async () => Number(await screen.getByTestId('ink').textContent()), { timeout: 30_000 })
    .toBeGreaterThan(3_000);
  const page1 = await body();

  // Swipe forward to page 2, then back to page 1.
  const box = await panel.boundingBox();
  if (!box) throw new Error('no panel on the page');
  const beforeSwipe = await frames.textContent();
  await screen.swipe({ from: { x: box.x + 420, y: box.y + 420 }, to: { x: box.x + 100, y: box.y + 420 } });
  await expect(frames).not.toHaveText(beforeSwipe ?? '', { timeout: 30_000 });
  await expect(busy).toHaveText('no', { timeout: 60_000 });
  expect(await body(), 'swipe moved to page 2').not.toEqual(page1);

  const beforeBackSwipe = await frames.textContent();
  await screen.swipe({ from: { x: box.x + 100, y: box.y + 420 }, to: { x: box.x + 420, y: box.y + 420 } });
  await expect(frames).not.toHaveText(beforeBackSwipe ?? '', { timeout: 30_000 });
  await expect(busy).toHaveText('no', { timeout: 60_000 });
  await expect.poll(body, { timeout: 30_000 }).toEqual(page1);

  // Centre tap opens Book Menu popup (rows: Select Chapter, Text Settings, Reading Themes, Delete Book).
  const beforeMenu = await frames.textContent();
  await tapOnce(240, 400);
  await expect(frames).not.toHaveText(beforeMenu ?? '', { timeout: 30_000 });

  // Row 1 (Text Settings) sits centered around y = 397 inside the centered 4-row popup.
  await tapOnce(240, 397);
  await expect(activity).toHaveText('TextSettings', { timeout: 30_000 });

  // Back exits TextSettings back to MarkdownReader (popup reopened); tapping outside dismisses popup to page 1.
  const beforeBack = await frames.textContent();
  await tapOnce(60, 780); // Back
  await expect(frames).not.toHaveText(beforeBack ?? '', { timeout: 30_000 });
  await tapOnce(240, 120); // outside popup -> dismiss back to page
  await expect.poll(body, { timeout: 30_000 }).toEqual(page1);

  await screen.getByRole('button', { name: 'Home' }).click();
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});
