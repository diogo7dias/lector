#!/usr/bin/env python3
"""Run the bench firmware for one env in the lector QEMU (sim/.qemu, -icount as sim/lector_sim.py)
and save its RESULT lines to results/<env>.txt.  Usage: run.py c3|s3"""
import os
import subprocess
import sys
import time

BENCH = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(BENCH)))
QEMU = os.path.join(REPO, "sim", ".qemu", "build")
MACHINES = {
    "c3": ("esp32c3", "qemu-system-riscv32", ["-machine", "esp32c3"]),
    "s3": ("esp32s3", "qemu-system-xtensa", ["-machine", "esp32s3", "-m", "8M", "-global", "ssi_psram.is_octal=true"]),
}


def main(env):
    chip, qemu, machine = MACHINES[env]
    build = os.path.join(BENCH, ".pio", "build", env)
    flash = os.path.join(build, "flash.bin")
    py = os.path.expanduser("~/.platformio/penv/bin/python")
    subprocess.run([py, "-m", "esptool", "--chip", chip, "merge-bin", "--fill-flash-size", "16MB", "-o", flash,
                    "0x0", f"{build}/bootloader.bin", "0x8000", f"{build}/partitions.bin",
                    "0x10000", f"{build}/firmware.bin"], check=True, stdout=subprocess.DEVNULL)
    log = os.path.join(build, "uart0.log")
    proc = subprocess.Popen([os.path.join(QEMU, qemu), "-nographic", *machine, "-icount", "shift=3",
                             "-drive", f"file={flash},if=mtd,format=raw", "-serial", f"file:{log}",
                             "-monitor", "none"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.time() + 3600
    try:
        while time.time() < deadline:
            time.sleep(2)
            text = open(log, errors="replace").read() if os.path.exists(log) else ""
            if "DONE" in text or proc.poll() is not None:
                break
    finally:
        proc.kill()
    keep = ("BENCH", "CALIB", "START", "RESULT", "PANIC", "NOTASK", "DONE", "memory allocation")
    lines = [ln for ln in text.splitlines() if ln.startswith(keep)]
    os.makedirs(os.path.join(BENCH, "results"), exist_ok=True)
    out = os.path.join(BENCH, "results", f"{env}.txt")
    open(out, "w").write("\n".join(lines) + "\n")
    print(f"{out}: {sum(ln.startswith('RESULT') for ln in lines)} results, done={'DONE' in text}")


if __name__ == "__main__":
    main(sys.argv[1])
