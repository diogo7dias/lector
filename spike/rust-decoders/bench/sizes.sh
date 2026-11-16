#!/usr/bin/env bash
# Flash cost of each side as a whole (code, tables and strings): the bench image's flash
# sections with both sides, minus the same image built without one side.
set -euo pipefail
cd "$(dirname "$0")"
for env in c3 s3; do
  tool=$([ $env = c3 ] && echo riscv32-esp-elf-size || echo xtensa-esp32s3-elf-size)
  size=$(ls ~/.platformio/packages/toolchain-*/bin/$tool | head -1)
  for variant in both BENCH_WITHOUT_RUST BENCH_WITHOUT_C; do
    flags=$([ $variant = both ] && echo "" || echo "-D$variant")
    PLATFORMIO_BUILD_FLAGS="$flags" pio run -e $env -s >/dev/null
    echo "$env $variant $($size -A .pio/build/$env/firmware.elf |
      awk '$1 ~ /^\.(flash\.text|flash\.rodata|iram0\.text|dram0\.data)$/ {s += $2} END {print s}')"
  done
done
pio run -e c3 -s >/dev/null && pio run -e s3 -s >/dev/null
