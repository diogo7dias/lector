import type { E2EConfig } from 'e2e';
import { web } from '@e2e-dev/web';

// The real firmware (last `pio run -e default`) in QEMU, served as a device page. No model:
// every check is exact (activity name, frame count, ink on the panel).
export default {
  tests: 'tests/**/*.e2e.ts',
  workers: 1,
  timeout: 180_000,
  targets: [{
    engine: web({ viewport: { width: 520, height: 1100 } }),
    app: {
      url: 'http://127.0.0.1:0',
      command: {
        executable: 'python3',
        args: ['server.py', '{port}'],
        log: '.e2e/logs/sim.log',
        // The page answers only once the firmware has booted and drawn Home.
        startupTimeout: 180_000,
        ...(process.env.LECTOR_QEMU ? { env: { LECTOR_QEMU: process.env.LECTOR_QEMU } } : {}),
      },
    },
  }],
} satisfies E2EConfig;
