"""Refresh Wallpaper on the X4 Pro: one timed wake after a lock repaints the wallpaper and sleeps.

usage: python3 sim/wallpaper_refresh.py     (needs `pio run -e x4pro`)

Exit 1 unless the lock arms the timer, the wake comes a minute later without a key,
it draws the same picture, sleeps again and arms nothing further.
"""
import os, re, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
os.environ["LECTOR_SD_EXTRA"] = os.path.join(HERE, "fixtures/wallpaper-refresh")
from lector_sim import LectorSim, make_flash, make_sd  # noqa: E402


def wait(sim, what, done, timeout):
    end = time.time() + timeout
    while time.time() < end:
        if done(sim.log_text()):
            return
        time.sleep(1)
    sys.exit(f"FAIL: {what} (waited {timeout} s)")


def stamp(log, line):
    return int(re.findall(r"\[(\d+)\] \[INF\] \[\w+\] " + re.escape(line), log)[-1])


def main():
    work = tempfile.mkdtemp(prefix="lector-wallpaper-refresh-")
    sim = LectorSim("x4pro", make_flash(os.path.join(work, "flash.bin"), "x4pro"),
                    make_sd(os.path.join(work, "sd.img")), work)
    assert sim.boot(timeout=120), "no first frame"
    try:
        sim.press("power", hold=1.2, settle=False)
        wait(sim, "lock arms the refresh", lambda log: "Wallpaper refresh armed" in log and sim.asleep(), 120)
        locked = sim.screenshot(os.path.join(work, "locked.png"))
        slept_at = stamp(sim.log_text(), "Entering deep sleep")
        wait(sim, "timed wake repaints and sleeps",
             lambda log: "Wallpaper refresh wake" in log and log.count("Entering deep sleep") == 2 and sim.asleep(), 600)
        log = sim.log_text()
        rested = stamp(log, "Wallpaper refresh wake") - slept_at
        assert 55_000 <= rested <= 70_000, f"woke after {rested} ms, expected about 60 000"
        assert log.count("Wallpaper refresh armed") == 1, "the refresh armed another one"
        assert "Frontlight up" not in log[log.rfind("Wallpaper refresh wake"):], "the refresh wake lit the frontlight"
        after = sim.screenshot(os.path.join(work, "refreshed.png"))
        assert open(locked, "rb").read() == open(after, "rb").read(), "the picture changed"
        time.sleep(90)
        assert sim.log_text().count("Wallpaper refresh wake") == 1, "a second refresh wake"
        # A Power press inside the armed minute is an ordinary wake, never a refresh.
        assert sim.power_on(timeout=120), "no wake from the refreshed sleep"
        wait(sim, "wake reaches a screen", lambda log: re.findall(r"Entering activity: (\w+)", log)[-1] != "Sleep", 120)
        sim.wait_idle()
        sim.press("power", hold=1.2, settle=False)
        wait(sim, "second lock arms", lambda log: log.count("Wallpaper refresh armed") == 2 and sim.asleep(), 120)
        assert sim.power_on(timeout=120), "no wake inside the armed minute"
        wait(sim, "armed wake reaches a screen",
             lambda log: re.findall(r"Entering activity: (\w+)", log)[-1] != "Sleep", 120)
        assert sim.log_text().count("Wallpaper refresh wake") == 1, "a key wake was taken for a refresh"
        print(f"ok: repaint {rested} ms after lock, same picture, asleep again; key wake stays a wake ({work})")
    finally:
        sim.stop()


if __name__ == "__main__":
    main()
