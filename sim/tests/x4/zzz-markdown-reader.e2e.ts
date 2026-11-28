import { test, expect } from 'e2e';

// Open a Markdown file from /read on X4 (button-driven), verify rendered text and page turn,
// open the Book Menu popup, enter Text Settings, and verify Back returns cleanly to the page.
// Named zzz- to run last on the board, starting where zz-back-returns.e2e.ts leaves Home (Settings row).
test('opens a Markdown file, turns a page, enters Text Settings, and returns via Back', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  const frames = screen.getByTestId('frames');
  await expect(activity).toHaveText('Home', { timeout: 60_000 });

  const press = async (name: string, times = 1) => {
    for (let i = 0; i < times; i++) {
      await screen.getByRole('button', { name }).click();
      await expect(busy).toHaveText('no', { timeout: 60_000 });
    }
  };
  // Page body as 20 px ink bands above the status bar (which tracks "+N" session pages).
  const body = async () => {
    const bands: number[] = [];
    for (let y = 0; y < 740; y += 20) {
      bands.push((await (await fetch(`${app.baseUrl}/ink/0/${y}/480/20`)).json()).ink);
    }
    return bands;
  };

  // Home starts on Settings (row 5) after zz-back-returns, or Browse Files (row 0) on a fresh boot.
  // Detect which by checking if Confirm opens Settings or FileBrowser; if Settings, Back and Up 5.
  await press('Confirm');
  await expect(activity).not.toHaveText('Home', { timeout: 30_000 });
  if ((await activity.textContent()) !== 'FileBrowser') {
    await press('Back');
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
    await press('Up', 5);
    await press('Confirm');
  }
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });

  // Card root rows: Search (0), [read] (1), ...
  await press('Down');
  await press('Confirm');
  // Inside /read: Search (0), [pics] (1), z_markdown_sample.md (2).
  await press('Down', 2);
  await press('Confirm');
  await expect(activity).toHaveText('MarkdownReader', { timeout: 60_000 });
  await expect(busy).toHaveText('no', { timeout: 30_000 });

  // Heading and formatted body render on page 1.
  await expect
    .poll(async () => Number(await screen.getByTestId('ink').textContent()), { timeout: 30_000 })
    .toBeGreaterThan(3_000);
  const page1 = await body();

  // Turn forward to page 2, then back to page 1.
  const beforeTurn = await frames.textContent();
  await press('Right');
  await expect(frames).not.toHaveText(beforeTurn ?? '', { timeout: 30_000 });
  expect(await body(), 'page turn moved to page 2').not.toEqual(page1);
  await press('Left');
  await expect.poll(body, { timeout: 30_000 }).toEqual(page1);

  // Confirm opens Book Menu popup (rows: Select Chapter, Text Settings, Reading Themes, Delete Book).
  const beforeMenu = await frames.textContent();
  await press('Confirm');
  await expect(frames).not.toHaveText(beforeMenu ?? '', { timeout: 30_000 });

  // Select Text Settings (row 1).
  await press('Down');
  await press('Confirm');
  await expect(activity).toHaveText('TextSettings', { timeout: 30_000 });

  // Back exits TextSettings back to MarkdownReader (with Book Menu reopened); Back again closes popup to page 1.
  const beforeBack = await frames.textContent();
  await press('Back');
  await expect(frames).not.toHaveText(beforeBack ?? '', { timeout: 30_000 });
  await press('Back');
  await expect.poll(body, { timeout: 30_000 }).toEqual(page1);

  // Back from MarkdownReader leaves the book cleanly back to FileBrowser -> Home.
  await press('Back');
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
  await press('Back');
  await press('Back');
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});
