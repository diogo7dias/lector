import { test, expect } from 'e2e';

// sim/fixtures/sd puts a credential bundle and a cut-short ".part" of one under
// /.crosspoint, as a power cut mid-share would leave them. They hold WiFi and OPDS
// passwords in the clear, so boot removes both before any screen opens.
test('boot removes a stale credential bundle and its partial copy', async ({ app, screen }) => {
  await app.open('/');
  await expect(screen.getByTestId('activity')).not.toHaveText('', { timeout: 120_000 });
  const log = async () => (await (await fetch(`${app.baseUrl}/log`)).text()) as string;
  for (const path of ['/.crosspoint/credentials.cpcred', '/.crosspoint/credentials.cpcred.part']) {
    await expect
      .poll(async () => (await log()).includes(`Removed stale credential bundle ${path}`), { timeout: 30_000 })
      .toBe(true);
  }
});
