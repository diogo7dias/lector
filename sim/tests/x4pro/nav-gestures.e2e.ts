import { test, expect } from 'e2e';

// Touch navigation on the X4 Pro: a flick over Home pages its books (and paints nothing when
// they all fit), a swipe up from the bottom edge opens the reader menu inside a book (both a
// fast flick and a slow pull from the bottom 14%) and goes Home outside a book, and the image
// viewer pages by the thirds of the screen with its actions behind a centre tap. The images
// are the card fixture /read/pics/{a,b,c}.bmp: 48 px squares, black centres of 40, 20 and 8 px.
test('home flick, bottom-edge menu/Home, image viewer taps', async ({ app, screen }) => {
  await app.open('/');
  const activity = screen.getByTestId('activity');
  const frames = screen.getByTestId('frames');
  const busy = screen.getByTestId('busy');
  await expect(activity).not.toHaveText('', { timeout: 120_000 });
  if ((await activity.textContent()) !== 'Home') {
    await screen.getByRole('button', { name: 'Home' }).click(); // an earlier suite left a book open
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
  }

  const panel = screen.getByRole('img', { name: 'Device screen' });
  const settle = () => expect(busy).toHaveText('no', { timeout: 60_000 });
  const tapOnce = async (x: number, y: number) => {
    await panel.tap({ position: { x, y } });
    await settle();
  };
  const post = async (path: string) => {
    expect((await fetch(`${app.baseUrl}${path}`, { method: 'POST' })).ok).toBe(true);
    await settle();
  };
  const ink = async (x: number, y: number, w: number, h: number) =>
    (await (await fetch(`${app.baseUrl}/ink/${x}/${y}/${w}/${h}`)).json()).ink as number;

  // The card holds two books, so Home fits every one of them: a flick has no page to go to
  // and must not repaint (before, it stepped the hidden selection a row and painted).
  const still = await frames.textContent();
  await post('/swipe/240/400/240/150');
  await expect(activity).toHaveText('Home');
  expect(await frames.textContent(), 'nothing to page, nothing painted').toBe(still);

  // Into a book, then open the reader menu via bottom-edge swipe up (flick + slow pull).
  // The suite shares one card: the book may already be Home's first row (blank on a fresh card).
  const listed = (await ink(0, 230, 480, 60)) > 500;
  if (listed) {
    await tapOnce(240, 259);
  } else {
    await tapOnce(240, 517); // Browse Files
    await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
    await tapOnce(150, 248); // test_jpeg_images.epub, whose menu image-filing.e2e.ts maps
  }
  await expect(activity).toHaveText('EpubReader', { timeout: 60_000 });
  await settle();
  const pageInk = await ink(0, 0, 480, 700);
  // A swipe up through the page body is not the gesture: the book stays open.
  await post('/swipe/240/600/240/300');
  await expect(activity).toHaveText('EpubReader');
  // Bottom-edge flick up inside a book opens the reader menu instead of going Home.
  await post('/swipe/240/795/240/450');
  await expect(activity).toHaveText('EpubReaderMenu', { timeout: 30_000 });
  await settle();
  await expect.poll(() => ink(0, 0, 480, 700), { timeout: 30_000 }).not.toBe(pageInk);
  // Back closes the menu back to the book, then a slow drag (>700 ms) from the bottom 14%
  // also opens the menu (activity only updates on onEnter(), so verify via ink + log).
  await tapOnce(60, 780);
  await expect.poll(() => ink(0, 0, 480, 700), { timeout: 30_000 }).toBe(pageInk);
  const log = async () => (await fetch(`${app.baseUrl}/log`)).text();
  const menuFrom = (await log()).length;
  await post('/swipe/240/720/240/450?ms=1200');
  await expect
    .poll(async () => (await log()).slice(menuFrom), { timeout: 30_000 })
    .toContain('Entering activity: EpubReaderMenu');
  await expect.poll(() => ink(0, 0, 480, 700), { timeout: 30_000 }).not.toBe(pageInk);
  if (!listed) {
    // Remove from Recents (in-book menu > Book) drops the book from Recents on exit and
    // leaves it unfiled at the card root, so later files still find /test_jpeg_images.epub.
    await tapOnce(100, 398); // Book (below expanded Navigate's 5 rows)
    await tapOnce(100, 318); // Remove from Recents
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
    expect(await ink(0, 230, 480, 60), 'removed from Home recents').toBeLessThan(500);
  } else {
    // Outside the reader activity (inside the menu), bottom-edge swipe up goes Home.
    await post('/swipe/240/795/240/450');
    await expect(activity).toHaveText('Home', { timeout: 30_000 });
  }

  // Browse Files > read > pics > a.bmp: each the first row under Search, y 124.
  await tapOnce(240, 517);
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
  await tapOnce(100, 124);
  await tapOnce(100, 124);
  await tapOnce(100, 124);
  await expect(activity).toHaveText('BmpViewer', { timeout: 30_000 });
  const square = () => ink(200, 360, 80, 80);
  await expect.poll(square, { timeout: 30_000 }).toBe(1600);
  await tapOnce(420, 300); // right third: next image
  await expect.poll(square, { timeout: 30_000 }).toBe(400);
  await tapOnce(60, 300); // left third: previous image
  await expect.poll(square, { timeout: 30_000 }).toBe(1600);

  // The centre opens the menu over the image; a tap outside it puts the image back.
  const outsideMenu = await ink(0, 0, 480, 300);
  await tapOnce(240, 400);
  await expect(activity).toHaveText('BmpViewer');
  await expect.poll(square, { message: 'the menu covers the centre', timeout: 30_000 }).not.toBe(1600);
  expect(await ink(0, 0, 480, 300), 'the image is still under the menu').toBe(outsideMenu);
  await tapOnce(240, 150);
  await expect.poll(square, { timeout: 30_000 }).toBe(1600);

  // Back on the band, then bottom-edge swipe up from FileBrowser goes Home.
  await tapOnce(60, 780);
  await expect(activity).toHaveText('FileBrowser', { timeout: 30_000 });
  await post('/swipe/240/795/240/450');
  await expect(activity).toHaveText('Home', { timeout: 30_000 });
});
