"""Drive the real lector firmware in QEMU: boot it, press its buttons, read its screen.

    sim = LectorSim(flash="firmware-merged.bin", sd="sd.img", workdir="/tmp/run")
    sim.boot()
    sim.press("down"); sim.press("confirm")
    sim.screenshot("home.png")
"""
import os, subprocess, time
from PIL import Image
from qmp import Qmp

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
QEMU = os.environ.get("LECTOR_QEMU", os.path.join(HERE, ".qemu/build/qemu-system-riscv32"))
BUILD = os.path.join(REPO, ".pio/build/default")
# Books the simulated card starts with.
SD_BOOKS = [os.path.join(REPO, "test/epubs", f) for f in ("test_kerning_ligature.epub", "test_jpeg_images.epub")]

PINS = (1 << 22) - 1
POWER_PIN, USB_PIN = 3, 20
IDLE_RAW = 4095
# Xteink X4 resistor ladder (InputManager.cpp): ADC channel and a raw reading inside each band.
BUTTONS = {
    "back": (1, 3512), "confirm": (1, 2694), "left": (1, 1493), "right": (1, 5),
    "up": (2, 2242), "down": (2, 5),
}


def make_flash(path):
    """Bootloader, partition table and app from the last `pio run -e default`, as one 16 MB image."""
    py = os.path.expanduser("~/.platformio/penv/bin/python")
    subprocess.run([py, "-m", "esptool", "--chip", "esp32c3", "merge-bin", "--fill-flash-size", "16MB",
                    "-o", path, "0x0", f"{BUILD}/bootloader.bin", "0x8000", f"{BUILD}/partitions.bin",
                    "0x10000", f"{BUILD}/firmware.bin"], check=True, stdout=subprocess.DEVNULL)
    return path


def make_sd(path, books=SD_BOOKS, size_mb=256):
    """A fresh FAT32 card holding `books`."""
    if os.path.exists(path):
        os.unlink(path)
    with open(path, "wb") as f:
        f.truncate(size_mb << 20)
    subprocess.run(["mkfs.vfat", "-F", "32", "-n", "LECTOR", path], check=True, stdout=subprocess.DEVNULL,
                   env={**os.environ, "PATH": os.environ.get("PATH", "") + ":/usr/sbin:/sbin"})
    if books:
        subprocess.run(["mcopy", "-i", path, *books, "::/"], check=True)
    return path


class LectorSim:
    def __init__(self, flash, sd, workdir, usb=False):
        self.flash, self.sd, self.dir, self.usb = flash, sd, workdir, usb
        os.makedirs(workdir, exist_ok=True)
        self.image = os.path.join(workdir, "screen.pgm")
        self.log = os.path.join(workdir, "usb.log")
        self.qemu = None
        self.q = None

    def _levels(self, power_held=False):
        levels = PINS
        if not self.usb:
            levels &= ~(1 << USB_PIN)
        if power_held:
            levels &= ~(1 << POWER_PIN)
        return levels

    def start(self):
        sock = os.path.join(self.dir, "qmp.sock")
        for f in (sock, self.image):
            if os.path.exists(f):
                os.unlink(f)
        self.qemu = subprocess.Popen(
            [QEMU, "-S", "-nographic", "-machine", "esp32c3",
             # Instruction-counted time: runs are deterministic and light sleep
             # can never overshoot FreeRTOS's tick budget the way host jitter does.
             "-icount", "shift=3,sleep=on",
             "-drive", f"file={self.flash},if=mtd,format=raw",
             "-drive", f"file={self.sd},if=sd,format=raw",
             "-global", f"ssd1677.image={self.image}",
             "-serial", f"file:{self.dir}/uart0.log", "-serial", "null",
             "-serial", f"file:{self.log}",
             "-monitor", "none", "-qmp", f"unix:{sock},server=on,wait=off"],
            stdout=subprocess.DEVNULL, stderr=open(os.path.join(self.dir, "qemu.err"), "w"))
        self.q = Qmp(sock)
        self.q.set("/machine/gpio", "levels", self._levels())

    def boot(self, timeout=60):
        """Power on the way a finger does: hold power, release once the screen is up."""
        if not self.qemu:
            self.start()
        self.q.set("/machine/gpio", "levels", self._levels(power_held=True))
        self.q.cmd("cont")
        time.sleep(1.5)
        self.q.set("/machine/gpio", "levels", self._levels())
        return self.wait_frames(1, timeout)

    def frames(self):
        return self.q.get("/machine/panel", "frames")

    def wait_frames(self, n, timeout=30):
        """Wait until the panel has shown at least n frames in total."""
        end = time.time() + timeout
        while time.time() < end:
            if self.frames() >= n:
                return True
            time.sleep(0.1)
        return False

    def wait_idle(self, quiet=2.0, timeout=60):
        """Wait until no new frame has appeared for `quiet` seconds."""
        end, last, since = time.time() + timeout, self.frames(), time.time()
        while time.time() < end:
            time.sleep(0.2)
            now = self.frames()
            if now != last:
                last, since = now, time.time()
            elif time.time() - since >= quiet:
                return True
        return False

    def press(self, button, hold=0.15, settle=True):
        """Press and release a button; by default wait for the screen it leads to."""
        before = self.frames()
        self._press(button, hold)
        if settle:
            self.wait_frames(before + 1, timeout=15)
            self.wait_idle()

    def _press(self, button, hold):
        if button == "power":
            self.q.set("/machine/gpio", "levels", self._levels(power_held=True))
            time.sleep(hold)
            self.q.set("/machine/gpio", "levels", self._levels())
            return
        ch, raw = BUTTONS[button]
        self.q.set("/machine/saradc", f"ch{ch}", raw)
        time.sleep(hold)
        self.q.set("/machine/saradc", f"ch{ch}", IDLE_RAW)

    def screen(self):
        """The panel as the reader sees it, upright (480x800)."""
        raw = Image.open(self.image)
        raw.load()
        return raw.transpose(Image.Transpose.FLIP_TOP_BOTTOM).rotate(-90, expand=True)

    def screenshot(self, path):
        self.screen().save(path)
        return path

    def log_text(self):
        with open(self.log, "rb") as f:
            return f.read().decode("utf-8", "replace")

    def stop(self):
        if self.qemu:
            self.qemu.terminate()
            self.qemu.wait()
            self.qemu = None
