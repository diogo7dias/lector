"""Boot a board again and again, then leave it idle, and count what goes wrong.

usage: python3 sim/soak.py BOARD [BOOTS] [IDLE_SECONDS]     (default 10 boots, 600 s idle)

A boot that never draws a frame is a hang; a reset (rst:0xc) or a crash report on
the card is a crash. Exit 1 if either happened.
"""
import os, re, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lector_sim import LectorSim, make_flash, make_sd  # noqa: E402


def run(board, boots=10, idle=600):
    work = tempfile.mkdtemp(prefix=f"lector-soak-{board}-")
    flash = make_flash(os.path.join(work, "flash.bin"), board)
    bad = 0
    for i in range(boots + 1):
        last = i == boots
        sim = LectorSim(board, flash, make_sd(os.path.join(work, "sd.img")), work)
        booted = sim.boot(timeout=60)
        if booted and last:
            time.sleep(idle)
        log = sim.log_text()
        sim.stop()
        card = subprocess.run(["mdir", "-b", "-i", os.path.join(work, "sd.img") + "@@1M", "::/crash_report.txt"],
                              capture_output=True).returncode == 0
        resets = log.count("rst:0xc")
        activities = re.findall(r"Entering activity: (\w+)", log)
        what = f"{'idle ' + str(idle) + ' s' if last else 'boot ' + str(i + 1)}: {'ok' if booted else 'HANG'}"
        print(f"{board} {what}, resets {resets}, crash report {card}, last activity {activities[-1:]}", flush=True)
        bad += (not booted) or resets > 0 or card
    print(f"{board}: {bad} bad of {boots + 1} runs ({work})")
    return bad


if __name__ == "__main__":
    args = sys.argv[1:]
    sys.exit(1 if run(args[0], *(int(a) for a in args[1:3])) else 0)
