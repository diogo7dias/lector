#!/usr/bin/env python3
"""Encode images into Lector .pxc wallpapers, one recipe at a time or a whole matrix.

    python3 make_pxc.py photo.jpg --device x4pro --out /Volumes/CARD/locklab
    python3 make_pxc.py photo.jpg --device x4pro --out ./locklab --matrix
    python3 make_pxc.py photo.jpg --device x4pro --out ./locklab --verify

A .pxc carries four grey levels and nothing else: no palette, no gamma, no compression.
That makes the encoder, not the firmware, the place where nearly all of a lock screen's
picture quality is decided, and it is why this exists next to the Lock Lab rather than
somewhere in a browser. The lab turns the knobs the device owns; this turns the ones the
file owns, and --matrix writes every combination as a separate file so the two can be
judged side by side on the panel.

Format, matching src/activities/boot_sleep/PxcSleepRenderer.h:
    uint16 width  (little endian)
    uint16 height (little endian)
    2 bits per pixel, 4 pixels per byte, MSB first within the byte, row-major,
    bytesPerRow = (width + 3) // 4, levels 0..3 meaning grey 0 / 85 / 170 / 255.

The panel size is not advice. renderPxcSleepScreen rejects anything more than one pixel
off and the device falls back to another sleep face, so an X4 wallpaper (480x800) simply
will not draw on an X4 Pro (800x480). --device is the whole fix for that.

Matrix output is named M01.PXC.. so it is already 8.3 and needs no second pass; for other
folders scripts/rename_wallpapers.py is the tool that shortens names and compacts the FAT
directory.
"""

import argparse
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

# Panel sizes, as authored. Width first.
DEVICES = {
    "x4pro": (800, 480),  # ESP32-S3, native landscape scan
    "x4": (480, 800),
    "x3": (528, 792),
}

DITHERS = ("none", "bayer4", "bayer8", "fs", "blue")

BAYER4 = np.array(
    [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], dtype=np.float64
) / 16.0


def bayer8() -> np.ndarray:
    """The 8x8 Bayer matrix, built from the 4x4 by the standard recursion."""
    base = np.array([[0, 2], [3, 1]], dtype=np.float64)
    m = base
    while m.shape[0] < 8:
        m = np.block([[4 * m, 4 * m + 2], [4 * m + 3, 4 * m + 1]])
    return m / m.size


def blue_noise(size: int = 64, sigma: float = 1.5, seed: int = 7) -> np.ndarray:
    """A void-and-cluster blue-noise threshold matrix, normalised to [0, 1).

    Ordered masks put their quantisation error on a lattice, and the eye is very good at
    finding lattices. Blue noise spreads the same error into the frequencies the eye is
    worst at, which is why a 4-level image dithered this way stops looking like fabric.
    """
    total = size * size
    rng = np.random.default_rng(seed)
    r = np.arange(size)
    r = np.minimum(r, size - r)
    kernel = np.exp(-(r[:, None] ** 2 + r[None, :] ** 2) / (2 * sigma * sigma))
    fk = np.fft.fft2(kernel)

    def energy(binary):
        return np.real(np.fft.ifft2(np.fft.fft2(binary) * fk))

    binary = np.zeros((size, size), dtype=int)
    binary.flat[rng.choice(total, size=total // 10, replace=False)] = 1
    while True:
        tight = np.argmax(np.where(binary == 1, energy(binary), -np.inf))
        binary.flat[tight] = 0
        loose = np.argmin(np.where(binary == 0, energy(binary), np.inf))
        if loose == tight:
            binary.flat[tight] = 1
            break
        binary.flat[loose] = 1

    proto = binary.copy()
    rank = np.full((size, size), -1, dtype=int)

    work = proto.copy()
    ones = int(work.sum())
    for i in range(ones - 1, -1, -1):
        tight = np.argmax(np.where(work == 1, energy(work), -np.inf))
        work.flat[tight] = 0
        rank.flat[tight] = i
    work = proto.copy()
    for i in range(ones, total // 2):
        void = np.argmin(np.where(work == 0, energy(work), np.inf))
        work.flat[void] = 1
        rank.flat[void] = i
    for i in range(total // 2, total):
        tight = np.argmax(np.where(work == 0, energy(1 - work), -np.inf))
        work.flat[tight] = 1
        rank.flat[tight] = i
    return rank / total


def to_linear(srgb: np.ndarray) -> np.ndarray:
    """sRGB 0..1 to linear light. Quantising in sRGB space darkens midtones."""
    return np.where(srgb <= 0.04045, srgb / 12.92, ((srgb + 0.055) / 1.055) ** 2.4)


def to_srgb(linear: np.ndarray) -> np.ndarray:
    return np.where(linear <= 0.0031308, linear * 12.92, 1.055 * linear ** (1 / 2.4) - 0.055)


def prepare(path: Path, size, fit: str, rotate: int) -> np.ndarray:
    """Load, orient, fit to the panel, and return linear-light luminance in 0..1."""
    image = Image.open(path)
    image = image.convert("L")
    if rotate:
        image = image.rotate(rotate, expand=True)
    target_w, target_h = size
    # A portrait source on a landscape panel is almost always meant to be turned rather
    # than letterboxed into a stripe, and the reverse likewise.
    if (image.width > image.height) != (target_w > target_h):
        image = image.rotate(90, expand=True)

    if fit == "cover":
        scale = max(target_w / image.width, target_h / image.height)
        new = (max(1, round(image.width * scale)), max(1, round(image.height * scale)))
        image = image.resize(new, Image.LANCZOS)
        left = (image.width - target_w) // 2
        top = (image.height - target_h) // 2
        image = image.crop((left, top, left + target_w, top + target_h))
    else:
        scale = min(target_w / image.width, target_h / image.height)
        new = (max(1, round(image.width * scale)), max(1, round(image.height * scale)))
        image = image.resize(new, Image.LANCZOS)
        canvas = Image.new("L", (target_w, target_h), 255)
        canvas.paste(image, ((target_w - image.width) // 2, (target_h - image.height) // 2))
        image = canvas

    return to_linear(np.asarray(image, dtype=np.float64) / 255.0)


def tone(linear: np.ndarray, auto_levels: bool, gamma: float, contrast: float) -> np.ndarray:
    """Black/white points, gamma and contrast, all in linear light."""
    out = linear
    if auto_levels:
        # 1st and 99th percentiles rather than min and max: one stuck white pixel should
        # not decide where the whole image's white point sits. Same idea as upstream
        # CrossPoint PR #2861's adaptive tone mapping, done before the file is written.
        low, high = np.percentile(out, (1.0, 99.0))
        if high - low > 1e-6:
            out = np.clip((out - low) / (high - low), 0.0, 1.0)
    if contrast != 1.0:
        out = np.clip((out - 0.5) * contrast + 0.5, 0.0, 1.0)
    if gamma != 1.0:
        out = np.clip(out, 0.0, 1.0) ** (1.0 / gamma)
    return np.clip(out, 0.0, 1.0)


def quantise(linear: np.ndarray, dither: str) -> np.ndarray:
    """Linear luminance to levels 0..3."""
    # Dither in perceptual space: the four levels are evenly spaced in sRGB (0/85/170/255),
    # so that is where the error has to be spread evenly.
    value = to_srgb(linear) * 3.0
    height, width = value.shape

    if dither == "none":
        return np.clip(np.rint(value), 0, 3).astype(np.uint8)

    if dither == "fs":
        # Floyd-Steinberg. Serial by nature, so this is the slow one; on a 800x480 panel
        # it is still under a second and it is usually the best-looking of the five.
        work = value.copy()
        out = np.zeros_like(work, dtype=np.uint8)
        for y in range(height):
            for x in range(width):
                old = work[y, x]
                new = min(3.0, max(0.0, round(old)))
                out[y, x] = int(new)
                error = old - new
                if x + 1 < width:
                    work[y, x + 1] += error * 7 / 16
                if y + 1 < height:
                    if x > 0:
                        work[y + 1, x - 1] += error * 3 / 16
                    work[y + 1, x] += error * 5 / 16
                    if x + 1 < width:
                        work[y + 1, x + 1] += error * 1 / 16
        return out

    if dither == "bayer4":
        mask = BAYER4
    elif dither == "bayer8":
        mask = bayer8()
    else:
        mask = blue_noise()
    tiled = np.tile(mask, (height // mask.shape[0] + 1, width // mask.shape[1] + 1))[:height, :width]
    # Ordered dithering proper: a pixel sitting f of the way between two levels lands on
    # the higher one for exactly the fraction f of the mask's cells, so the local average
    # is the original value rather than the nearest level.
    return np.clip(np.floor(value + tiled), 0, 3).astype(np.uint8)


def pack(levels: np.ndarray) -> bytes:
    """Levels 0..3 to the .pxc byte layout: 4 px/byte, MSB first, rows padded."""
    height, width = levels.shape
    bytes_per_row = (width + 3) // 4
    padded = np.zeros((height, bytes_per_row * 4), dtype=np.uint8)
    padded[:, :width] = levels
    groups = padded.reshape(height, bytes_per_row, 4).astype(np.uint8)
    packed = (groups[:, :, 0] << 6) | (groups[:, :, 1] << 4) | (groups[:, :, 2] << 2) | groups[:, :, 3]
    return struct.pack("<HH", width, height) + packed.astype(np.uint8).tobytes()


def unpack(blob: bytes) -> np.ndarray:
    """Decode a .pxc back to levels, the same way PxcSleepRenderer does."""
    width, height = struct.unpack_from("<HH", blob, 0)
    bytes_per_row = (width + 3) // 4
    body = np.frombuffer(blob, dtype=np.uint8, offset=4, count=bytes_per_row * height)
    rows = body.reshape(height, bytes_per_row)
    out = np.zeros((height, bytes_per_row * 4), dtype=np.uint8)
    for i in range(4):
        out[:, i::4] = (rows >> (6 - i * 2)) & 0x03
    return out[:, :width]


def write_pxc(path: Path, levels: np.ndarray, verify: bool) -> None:
    blob = pack(levels)
    path.write_bytes(blob)
    if verify:
        # Re-read rather than re-use the array in memory: the point is to prove the bytes
        # on the card decode to the picture that was meant, not that pack() agrees with
        # itself.
        back = unpack(path.read_bytes())
        if back.shape != levels.shape or not np.array_equal(back, levels):
            raise SystemExit(f"verify failed for {path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("sources", nargs="+", type=Path, help="image files to encode")
    parser.add_argument("--device", choices=sorted(DEVICES), default="x4pro", help="panel size to author for")
    parser.add_argument("--size", help="override as WIDTHxHEIGHT")
    parser.add_argument("--out", type=Path, default=Path("."), help="output directory")
    parser.add_argument("--fit", choices=("cover", "contain"), default="cover")
    parser.add_argument("--rotate", type=int, default=0, help="degrees, counter-clockwise, before fitting")
    parser.add_argument("--dither", choices=DITHERS, default="fs")
    parser.add_argument("--auto-levels", action="store_true", help="black/white points from the 1st/99th percentiles")
    parser.add_argument("--gamma", type=float, default=1.0)
    parser.add_argument("--contrast", type=float, default=1.0)
    parser.add_argument("--matrix", action="store_true", help="write every recipe, with a MANIFEST.txt")
    parser.add_argument("--verify", action="store_true", help="decode each file back and compare")
    args = parser.parse_args()

    if args.size:
        try:
            width, height = (int(part) for part in args.size.lower().split("x"))
        except ValueError:
            return parser.error("--size wants WIDTHxHEIGHT, e.g. 800x480")
        size = (width, height)
    else:
        size = DEVICES[args.device]

    args.out.mkdir(parents=True, exist_ok=True)

    if args.matrix:
        # One axis at a time against a fixed baseline, not a full cross product: five
        # dithers times three tone settings is fifteen files a tester can actually step
        # through on the device, where the full product would be hundreds.
        recipes = [(d, False, 1.0, 1.0) for d in DITHERS]
        recipes += [
            ("fs", True, 1.0, 1.0),
            ("fs", False, 1.2, 1.0),
            ("fs", False, 0.85, 1.0),
            ("fs", False, 1.0, 1.25),
            ("fs", True, 1.0, 1.25),
            ("blue", True, 1.0, 1.0),
            ("blue", True, 1.0, 1.25),
        ]
    else:
        recipes = [(args.dither, args.auto_levels, args.gamma, args.contrast)]

    manifest = []
    index = 0
    for source in args.sources:
        if not source.is_file():
            print(f"skipping {source}: not a file", file=sys.stderr)
            continue
        base = prepare(source, size, args.fit, args.rotate)
        for dither, auto_levels, gamma, contrast in recipes:
            levels = quantise(tone(base, auto_levels, gamma, contrast), dither)
            if args.matrix:
                index += 1
                name = f"M{index:02d}.PXC"
            else:
                name = (source.stem.upper()[:8] or "OUT") + ".PXC"
            out_path = args.out / name
            write_pxc(out_path, levels, args.verify)
            recipe = (
                f"{name}  {source.name}  {size[0]}x{size[1]}  dither={dither}  "
                f"auto_levels={int(auto_levels)}  gamma={gamma}  contrast={contrast}"
            )
            manifest.append(recipe)
            print(recipe)

    if args.matrix:
        (args.out / "MANIFEST.txt").write_text("\n".join(manifest) + "\n")
        print(f"\n{len(manifest)} files and MANIFEST.txt in {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
