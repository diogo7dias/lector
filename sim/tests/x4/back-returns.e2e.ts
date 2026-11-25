import { test, expect } from 'e2e';

// Back returns to the screen that opened the one on top, on the row that opened it. A screen
// a reader menu row opened comes back to the menu; choosing something there (a chapter) goes
// to the page. Settings > File Transfer comes back to Settings on its card.
//
// A screen that pops back logs no "Entering", so the activity label keeps the closed
// screen's name: a label that changes to the menu or to Settings is the screen that came back.
test('Back from a screen a menu opened lands on that menu, on its row', async ({ app, screen }) => {
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
  const log = async () => (await (await fetch(`${app.baseUrl}/log`)).text()) as string;
  const countOf = (text: string, needle: string) => text.split(needle).length - 1;

  // Home is on Browse Files, as the file before this one leaves it.
  await press('Confirm');
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
  await press('Up'); // the last book in the card's root
  await press('Confirm');
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });

  // Reader menu > Look > Reader Settings (Text Settings), then Back: the menu, not the page.
  await press('Confirm');
  await expect(activity).toHaveText('EpubReaderMenu', { timeout: 30_000 });
  await press('Up'); // from the Navigate heading to Look, the first heading
  await press('Confirm'); // open Look
  await press('Down'); // Reader Settings, its first row
  await press('Confirm');
  await expect(activity).toHaveText('TextSettings', { timeout: 30_000 });
  await press('Back');
  await expect(activity, 'Back from Text Settings lands on the reader menu').toHaveText('EpubReaderMenu', {
    timeout: 30_000,
  });
  // The menu came back on the row that opened Text Settings: Confirm opens it again.
  await press('Confirm');
  await expect(activity).toHaveText('TextSettings', { timeout: 30_000 });
  await press('Back');
  await expect(activity).toHaveText('EpubReaderMenu', { timeout: 30_000 });

  // Navigate > Select Chapter: Back lands on the menu, picking a chapter lands on the page.
  await press('Back'); // closes Look, onto its heading
  await press('Down'); // Navigate
  await press('Confirm');
  await press('Down'); // Select Chapter
  await press('Confirm');
  await expect(activity).toHaveText('EpubReaderChapterSelection', { timeout: 30_000 });
  await press('Back');
  await expect(activity, 'Back from the chapter list lands on the reader menu').toHaveText('EpubReaderMenu', {
    timeout: 30_000,
  });
  await press('Confirm'); // Select Chapter again, from the row the menu came back on
  await expect(activity).toHaveText('EpubReaderChapterSelection', { timeout: 30_000 });
  const menusBefore = countOf(await log(), 'Entering activity: EpubReaderMenu');
  await press('Down');
  const before = (await log()).length;
  await press('Confirm'); // a chapter: the page, not the menu
  const sincePick = async () => (await log()).slice(before);
  await expect
    .poll(async () => (await sincePick()).includes('Exiting activity: EpubReaderChapterSelection'), { timeout: 30_000 })
    .toBe(true);
  await expect.poll(async () => (await sincePick()).includes('Rendered page'), { timeout: 60_000 }).toBe(true);
  expect(countOf(await log(), 'Entering activity: EpubReaderMenu'), 'a picked chapter goes to the page').toBe(
    menusBefore,
  );

  // The book's Back returns where it was opened, the browser, and the browser's Back goes Home.
  await press('Back');
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
  await press('Back');
  await expect(activity).toHaveText('Home', { timeout: 30_000 });

  // Home's Nearby choice > Send a file opens the browser in its place; Back rebuilds the choice.
  await press('Down', 3); // Nearby Sync, after Library and OPDS Browser
  await press('Confirm');
  await expect(activity).toHaveText('NearbyFileTransfer', { timeout: 30_000 });
  await press('Down', 2); // Send, after the two receives
  await press('Confirm');
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
  await press('Back');
  await expect(activity, 'Back from the browser lands on the Nearby choice').toHaveText('NearbyFileTransfer', {
    timeout: 30_000,
  });
  await press('Back');
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
  await press('Up', 3); // Home came back on Nearby Sync; Browse Files is three rows up

  // Settings > File Transfer, then Back out of its mode list: Settings, on the File Transfer card.
  await press('Down', 5); // Settings, from Browse Files
  await press('Confirm');
  await expect(activity).toHaveText('Settings', { timeout: 30_000 });
  await press('Down', 4); // File Transfer, the fifth card
  await press('Confirm');
  await expect(activity).toHaveText('NetworkModeSelection', { timeout: 60_000 });
  await press('Back');
  await expect(activity, 'Back from File Transfer lands on Settings').toHaveText('Settings', { timeout: 60_000 });
  await press('Confirm'); // the card it came back on
  await expect(activity).toHaveText('NetworkModeSelection', { timeout: 60_000 });
  await press('Back');
  await expect(activity).toHaveText('Settings', { timeout: 60_000 });
  await press('Back');
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
  await press('Up', 5); // back onto Browse Files for the next file
});
