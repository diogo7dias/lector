import { test, expect } from 'e2e';

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

// A chapter still being laid out parks its build a few pages ahead of the reader. Parked,
// nothing is working, so a reader left idle must still drop to low power. Continues in the
// book the test above opened: the suite shares one running device.
test('an idle reader in a chapter whose build is parked drops to low power', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const busy = screen.getByTestId('busy');
  await expect(activity).toHaveText('EpubReader', { timeout: 30_000 });

  const press = async (name: string, times = 1) => {
    for (let i = 0; i < times; i++) {
      await screen.getByRole('button', { name }).click();
      await expect(busy).toHaveText('no', { timeout: 60_000 });
    }
  };
  const sinceChapterOpened = async () => {
    const text = await (await fetch(`${app.baseUrl}/log`)).text();
    const opened = text.lastIndexOf('Loading file: OEBPS/chapter8.xhtml');
    return opened < 0 ? '' : text.slice(opened);
  };

  await press('Confirm');
  await expect(activity).toHaveText('EpubReaderMenu', { timeout: 30_000 });
  await press('Confirm'); // expand Navigate
  await press('Down'); // Select Chapter
  await press('Confirm');
  await expect(activity).toHaveText('EpubReaderChapterSelection', { timeout: 30_000 });
  await press('Down', 7); // Chapter 8, the longest in the book
  await press('Confirm');
  // The reader underneath resumes without a new "Entering activity" line, so the chapter load is the signal.
  await expect.poll(async () => (await sinceChapterOpened()) !== '', { timeout: 60_000 }).toBe(true);

  await expect
    .poll(async () => (await sinceChapterOpened()).includes('Going to low-power mode'), {
      message: 'the idle reader went to low power',
      timeout: 60_000,
    })
    .toBe(true);

  const built = await sinceChapterOpened();
  expect(built, 'the first chunk was laid out').toContain('Page 7 processed');
  expect(built, 'the build is still parked, so this idle proved the parked case').not.toContain('Page 8 processed');
});
