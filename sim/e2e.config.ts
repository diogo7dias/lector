import type { E2EConfig } from 'e2e';
import { web } from '@e2e-dev/web';

// The real firmware of one board (LECTOR_BOARD, default x4pro; built by `pio run -e <env>`)
// in QEMU, served as a device page. No model: every check is exact (activity name, frame
// count, ink on the panel).
const board = process.env.LECTOR_BOARD ?? 'x4pro';

export default {
  // The UC8279 X3 runs the X3 suite.
  tests: `tests/${board === 'x3uc8279' ? 'x3' : board}/**/*.e2e.ts`,
  workers: 1,
  timeout: 240_000,
  targets: [{
    engine: web({ viewport: { width: 520, height: 1100 } }),
    app: {
      url: 'http://127.0.0.1:0',
      command: {
        executable: 'python3',
        args: ['server.py', '{port}'],
        log: `.e2e/logs/sim-${board}.log`,
        // The page answers only once the firmware has booted and drawn Home.
        startupTimeout: 180_000,
        env: {
          LECTOR_BOARD: board,
          ...(process.env.LECTOR_QEMU_DIR ? { LECTOR_QEMU_DIR: process.env.LECTOR_QEMU_DIR } : {}),
        },
      },
    },
  }],
} satisfies E2EConfig;
