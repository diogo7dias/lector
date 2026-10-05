"""Drive the real lector firmware in QEMU: boot it, press its buttons, touch it, read its screen.

    sim = LectorSim("x4pro", flash="firmware-merged.bin", sd="sd.img", workdir="/tmp/run")
    sim.boot()
    sim.tap(240, 570); sim.press("down")
    sim.screenshot("home.png")
"""
import os, subprocess, sys, time
from PIL import Image
from qmp import Qmp

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
QEMU_DIR = os.environ.get("LECTOR_QEMU_DIR", os.path.join(HERE, ".qemu/build"))
# Books the simulated card starts with.
SD_BOOKS = [os.path.join(REPO, "test/epubs", f) for f in ("test_kerning_ligature.epub", "test_jpeg_images.epub")]
IDLE_RAW = 4095

# One entry per simulated device. Pins and ladders come from BoardConfig.h / InputManager.cpp.
BOARDS = {
    "x4": {
        "qemu": "qemu-system-riscv32", "machine": ["-machine", "esp32c3"], "panel": "ssd1677",
        "env": "default", "chip": "esp32c3", "gpio": "/machine/gpio", "pins": 22, "uarts": 2,
        "power": 3, "usb": 20,
        # Resistor ladder: ADC channel and a raw reading inside each band.
        "ladder": {"back": (1, 3512), "confirm": (1, 2694), "left": (1, 1493), "right": (1, 5),
                   "up": (2, 2242), "down": (2, 5)},
    },
    # Same C3 binary as the X4; it tells itself apart by the X3's I2C chips.
    "x3": {
        "qemu": "qemu-system-riscv32", "machine": ["-machine", "esp32c3,x3=on"], "panel": "uc8253",
        "env": "default", "chip": "esp32c3", "gpio": "/machine/gpio", "pins": 22, "uarts": 2,
        "power": 3, "usb": None,
        "ladder": {"back": (1, 3512), "confirm": (1, 2694), "left": (1, 1493), "right": (1, 5),
                   "up": (2, 2242), "down": (2, 5)},
    },
    "x4pro": {
        "qemu": "qemu-system-xtensa",
        "machine": ["-machine", "esp32s3", "-m", "8M", "-global", "ssi_psram.is_octal=true"], "panel": "ssd1677",
        "env": "x4pro", "chip": "esp32s3", "gpio": "/machine/soc/gpio", "pins": 49, "uarts": 3,
        "power": 3, "usb": None,
        # Two active-low keys; back and confirm come from the touch panel and its Home key.
        "keys": {"up": 0, "down": 7}, "touch": True,
    },
}


def buttons(board):
    b = BOARDS[board]
    return [*b.get("ladder", {}), *b.get("keys", {}), *(["home"] if b.get("touch") else []), "power"]


def make_flash(path, board="x4pro"):
    """Bootloader, partition table and app from the last `pio run -e <env>`, as one 16 MB image."""
    b = BOARDS[board]
    build = os.path.join(REPO, ".pio/build", b["env"])
    # PlatformIO's venv carries esptool; elsewhere (CI) it is pip-installed next to us.
    py = os.path.expanduser("~/.platformio/penv/bin/python")
    py = py if os.path.exists(py) else sys.executable
    subprocess.run([py, "-m", "esptool", "--chip", b["chip"], "merge-bin", "--fill-flash-size", "16MB",
                    "-o", path, "0x0", f"{build}/bootloader.bin", "0x8000", f"{build}/partitions.bin",
                    "0x10000", f"{build}/firmware.bin"], check=True, stdout=subprocess.DEVNULL)
    return path


def make_sd(path, books=SD_BOOKS, size_mb=256):
    """A fresh card like a shop-bought one: an MBR with one FAT32 partition holding `books`.

    The X4 Pro mounts partition 1 only; a bare superfloppy fails there."""
    if os.path.exists(path):
        os.unlink(path)
    with open(path, "wb") as f:
        f.truncate(size_mb << 20)
    env = {**os.environ, "PATH": os.environ.get("PATH", "") + ":/usr/sbin:/sbin"}
    subprocess.run(["sfdisk", "-q", path], input=b"2048,,c\n", check=True, env=env)
    subprocess.run(["mkfs.vfat", "-F", "32", "-n", "LECTOR", "--offset", "2048", path, str((size_mb << 10) - 1024)],
                   check=True, stdout=subprocess.DEVNULL, env=env)
    if books:
        subprocess.run(["mcopy", "-i", f"{path}@@1M", *books, "::/"], check=True)
    return path


class LectorSim:
    def __init__(self, board, flash, sd, workdir, usb=False):
        self.board, self.b = board, BOARDS[board]
        self.flash, self.sd, self.dir, self.usb = flash, sd, workdir, usb
        os.makedirs(workdir, exist_ok=True)
        self.image = os.path.join(workdir, "screen.pgm")
        self.log = os.path.join(workdir, "usb.log")
        self.qemu = None
        self.q = None
        self.held = set()

    def _levels(self):
        levels = (1 << self.b["pins"]) - 1
        if self.b["usb"] is not None and not self.usb:
            levels &= ~(1 << self.b["usb"])
        for pin in self.held:
            levels &= ~(1 << pin)
        return levels

    def _hold(self, pin, down):
        (self.held.add if down else self.held.discard)(pin)
        self.q.set(self.b["gpio"], "levels", self._levels())

    def start(self):
        sock = os.path.join(self.dir, "qmp.sock")
        for f in (sock, self.image):
            if os.path.exists(f):
                os.unlink(f)
        # UART0 to a file, the other UARTs nowhere, then USB-serial-JTAG (the log).
        serials = ["-serial", f"file:{self.dir}/uart0.log"] + ["-serial", "null"] * (self.b["uarts"] - 1)
        self.qemu = subprocess.Popen(
            [os.path.join(QEMU_DIR, self.b["qemu"]), "-S", "-nographic",
             *self.b["machine"],
             # Instruction-counted time: runs are deterministic, and light sleep jumps
             # straight to its wake event, paced to the wall clock.
             "-icount", "shift=3,sleep=on",
             "-drive", f"file={self.flash},if=mtd,format=raw",
             "-drive", f"file={self.sd},if=sd,format=raw",
             "-global", f"{self.b['panel']}.image={self.image}",
             *serials, "-serial", f"file:{self.log}",
             "-monitor", "none", "-qmp", f"unix:{sock},server=on,wait=off"],
            stdout=subprocess.DEVNULL, stderr=open(os.path.join(self.dir, "qemu.err"), "w"))
        self.q = Qmp(sock)
        self.q.set(self.b["gpio"], "levels", self._levels())

    def boot(self, timeout=60):
        """Power on the way a finger does: hold power, release once the screen is up."""
        if not self.qemu:
            self.start()
        self.q.cmd("cont")
        return self.power_on(timeout)

    def power_on(self, timeout=60):
        """Hold power until the screen changes, then let go: a boot, or a wake from
        sleep. Emulated boots run slower than the wall clock, so no fixed hold fits."""
        before = self.frames()
        self._hold(self.b["power"], True)
        shown = self.wait_frames(before + 1, timeout)
        self._hold(self.b["power"], False)
        return shown

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
        if button == "home":
            self.q.set("/machine/touch", "home", True)
            time.sleep(hold)
            self.q.set("/machine/touch", "home", False)
        elif button in self.b.get("ladder", {}):
            ch, raw = self.b["ladder"][button]
            self.q.set("/machine/saradc", f"ch{ch}", raw)
            time.sleep(hold)
            self.q.set("/machine/saradc", f"ch{ch}", IDLE_RAW)
        else:
            pin = self.b["power"] if button == "power" else self.b["keys"][button]
            self._hold(pin, True)
            time.sleep(hold)
            self._hold(pin, False)

    def _finger(self, x, y):
        # The GT911 sits portrait under the landscape panel (swapXY + flipY in BoardConfig.h):
        # its X is the upright X, its Y the upright Y.
        self.q.set("/machine/touch", "x", x)
        self.q.set("/machine/touch", "y", y)

    def tap(self, x, y, hold=0.1, settle=True):
        """Touch the upright screen (480x800) at x, y and lift."""
        self.swipe(x, y, x, y, hold, settle)

    def swipe(self, x1, y1, x2, y2, duration=0.3, settle=True):
        """Put a finger down at x1, y1, slide it to x2, y2 over `duration`, lift."""
        before = self.frames()
        steps = 1 if (x1, y1) == (x2, y2) else 8
        self._finger(x1, y1)
        self.q.set("/machine/touch", "down", True)
        for i in range(1, steps + 1):
            time.sleep(duration / steps)
            self._finger(x1 + (x2 - x1) * i // steps, y1 + (y2 - y1) * i // steps)
        self.q.set("/machine/touch", "down", False)
        if settle:
            self.wait_frames(before + 1, timeout=15)
            self.wait_idle()

    def screen(self):
        """The panel as the reader sees it, upright (X4/X4 Pro 480x800, X3 528x792)."""
        raw = Image.open(self.image)
        raw.load()
        if self.b["panel"] == "ssd1677":
            raw = raw.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        return raw.rotate(-90, expand=True)

    def screenshot(self, path):
        self.screen().save(path)
        return path

    def asleep(self):
        """In deep sleep: the last boot has since handed over to it."""
        log = self.log_text()
        return log.rfind("Entering deep sleep") > log.rfind("entry 0x")

    def reboot(self, timeout=60):
        """Cut the power and switch on again; flash (settings, NVS) and card persist."""
        self.stop()
        return self.boot(timeout)

    def log_text(self):
        with open(self.log, "rb") as f:
            return f.read().decode("utf-8", "replace")

    def stop(self):
        if self.qemu:
            self.qemu.terminate()
            self.qemu.wait()
            self.qemu = None
