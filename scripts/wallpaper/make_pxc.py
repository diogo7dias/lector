#!/usr/bin/env python3
"""Encode images into Lector .pxc wallpapers, one recipe at a time or a whole matrix.

    python3 make_pxc.py photo.jpg --device x4pro --out /Volumes/CARD/locklab
    python3 make_pxc.py photo.jpg --device x4pro --out ./locklab --matrix
    python3 make_pxc.py --selfcheck

A .pxc carries four grey levels and nothing else, so the encoder decides nearly all of
a wallpaper's quality. --matrix writes every recipe as its own file to compare on the
panel.

Format, matching src/activities/boot_sleep/PxcSleepRenderer.h: uint16 width (LE),
uint16 height (LE), then 2 bits per pixel, 4 pixels per byte, MSB first, row-major,
bytesPerRow = (width + 3) // 4, levels 0..3.

Four things decide quality, and this used to get all four wrong:

  1. The levels are not evenly spaced in light. The firmware's display-tuned ladder
     (lib/GfxRenderer/BitmapHelpers.h, quantizeGray4 DisplayTuned) puts them at roughly
     sRGB 15/30/80/210: both mid greys are dark and the panel's white is not the
     image's 255. --levels panel is that ladder, nominal the old even spacing, and four
     sRGB bytes take a measured one.
  2. Dithering must average in light along that ladder, or a patch prints darker than
     asked. FS and Atkinson run on the level axis, serpentine.
  3. Tone is perceptual: percentiles, gamma and contrast work on sRGB values, then fit
     to the panel's black and white.
  4. Resampling is linear-light Lanczos with an unsharp pass either side, and a source
     that is not an image at all (alpha, EXIF rotation) is flattened before any of it.

renderPxcSleepScreen rejects anything more than one pixel off the panel, so an X4
wallpaper (480x800) will not draw on an X4 Pro (800x480); --device is the whole fix.
"""

import argparse
import math
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageOps

# Panel sizes, as authored. Width first.
DEVICES = {
    "x4pro": (800, 480),  # ESP32-S3, native landscape scan
    "x4": (480, 800),
    "x3": (528, 792),
}

DITHERS = ("none", "bayer4", "bayer8", "fs", "atkinson", "blue")

# The levels as the panel shows them, in sRGB 8-bit, straight from quantizeGray4's
# display-tuned ladder (lib/GfxRenderer/BitmapHelpers.h). "nominal" is the old assumption.
LEVEL_SETS = {"panel": (15, 30, 80, 210), "nominal": (0, 85, 170, 255)}

REC709 = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)

BAYER4 = np.array(
    [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], dtype=np.float64
) / 16.0

# Error diffusion kernels: (row, column, weight). Atkinson passes on only 6/8 of the
# error, trading gradient smoothness for a cleaner grain.
FS_KERNEL = ((0, 1, 7 / 16), (1, -1, 3 / 16), (1, 0, 5 / 16), (1, 1, 1 / 16))
ATKINSON_KERNEL = ((0, 1, 1 / 8), (0, 2, 1 / 8), (1, -1, 1 / 8), (1, 0, 1 / 8), (1, 1, 1 / 8), (2, 0, 1 / 8))


def bayer8() -> np.ndarray:
    base = np.array([[0, 2], [3, 1]], dtype=np.float64)
    m = base
    while m.shape[0] < 8:
        m = np.block([[4 * m, 4 * m + 2], [4 * m + 3, 4 * m + 1]])
    return m / m.size


def blue_noise(size: int = 64, sigma: float = 1.5, seed: int = 7) -> np.ndarray:
    """A void-and-cluster blue-noise threshold matrix in [0, 1).

    Ordered masks put their error on a lattice and the eye is a lattice detector.
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


def srgb_to_linear(v: np.ndarray) -> np.ndarray:
    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4).astype(np.float32)


def linear_to_srgb(v: np.ndarray) -> np.ndarray:
    v = np.clip(v, 0.0, 1.0).astype(np.float32)
    return np.where(v <= 0.0031308, v * 12.92, 1.055 * v ** (1 / 2.4) - 0.055).astype(np.float32)


def parse_levels(spec: str) -> np.ndarray:
    if spec in LEVEL_SETS:
        values = LEVEL_SETS[spec]
    else:
        try:
            values = tuple(int(part) for part in spec.split(","))
        except ValueError:
            raise SystemExit("--levels wants panel, nominal or four sRGB bytes, e.g. 15,30,80,210")
        if len(values) != 4 or list(values) != sorted(values) or not all(0 <= v <= 255 for v in values):
            raise SystemExit("--levels wants four increasing sRGB bytes in 0..255")
    return srgb_to_linear(np.array(values, dtype=np.float32) / 255.0)


def load_rgb(path: Path, size, rotate: int) -> Image.Image:
    image = ImageOps.exif_transpose(Image.open(path))
    if image.mode in ("RGBA", "LA", "PA") or (image.mode == "P" and "transparency" in image.info):
        image = image.convert("RGBA")
        backdrop = Image.new("RGBA", image.size, (255, 255, 255, 255))
        image = Image.alpha_composite(backdrop, image)
    image = image.convert("RGB")
    if rotate:
        image = image.rotate(rotate, expand=True)
    # A portrait source on a landscape panel is meant to be turned, not letterboxed.
    target_w, target_h = size
    if (image.width > image.height) != (target_w > target_h):
        image = image.rotate(90, expand=True)
    return image


def linear_luma(image: Image.Image) -> np.ndarray:
    """Rec.709 luminance in linear light, banded so a 24MP source fits in memory."""
    lut = srgb_to_linear(np.arange(256, dtype=np.float32) / 255.0)
    pixels = np.asarray(image, dtype=np.uint8)
    out = np.empty(pixels.shape[:2], dtype=np.float32)
    for y in range(0, pixels.shape[0], 256):
        out[y : y + 256] = lut[pixels[y : y + 256]] @ REC709
    return out


def fit_scale(shape, size, mode: str) -> float:
    height, width = shape
    target_w, target_h = size
    if mode == "cover":
        return max(target_w / width, target_h / height)
    return min(target_w / width, target_h / height)


def _blur121(p: np.ndarray) -> np.ndarray:
    out = p
    for axis in (0, 1):
        if axis == 1:
            padded = np.pad(out, ((0, 0), (1, 1)), mode="edge")
            out = (padded[:, :-2] + 2 * padded[:, 1:-1] + padded[:, 2:]) / 4.0
        else:
            padded = np.pad(out, ((1, 1), (0, 0)), mode="edge")
            out = (padded[:-2] + 2 * padded[1:-1] + padded[2:]) / 4.0
    return out


def sharpen(p: np.ndarray, amount: float, passes: int = 1) -> np.ndarray:
    if amount <= 0:
        return p
    blur = p
    for _ in range(passes):
        blur = _blur121(blur)
    return np.clip(p + amount * (p - blur), 0.0, 1.0).astype(np.float32)


def fit_linear(lin: np.ndarray, size, mode: str, scale: float) -> np.ndarray:
    height, width = lin.shape
    target_w, target_h = size
    if mode == "cover":
        new = (max(target_w, math.ceil(width * scale)), max(target_h, math.ceil(height * scale)))
    else:
        new = (min(target_w, max(1, round(width * scale))), min(target_h, max(1, round(height * scale))))
    plane = Image.fromarray(np.ascontiguousarray(lin, dtype=np.float32), mode="F")
    resized = np.array(plane.resize(new, Image.LANCZOS), dtype=np.float32)
    # Lanczos rings; a reflected pixel cannot be brighter than paper or darker than ink.
    np.clip(resized, 0.0, 1.0, out=resized)
    if mode == "cover":
        left = (new[0] - target_w) // 2
        top = (new[1] - target_h) // 2
        return resized[top : top + target_h, left : left + target_w]
    canvas = np.ones((target_h, target_w), dtype=np.float32)
    x = (target_w - new[0]) // 2
    y = (target_h - new[1]) // 2
    canvas[y : y + new[1], x : x + new[0]] = resized
    return canvas


def tone(p: np.ndarray, light: np.ndarray, auto_levels: bool, gamma: float, contrast: float) -> np.ndarray:
    """Perceptual tone, then fit the picture onto the panel's own black and white.

    Level 0 is not ink at 0 and level 3 is not paper at 1.0; printing into 0..1 would
    push everything above the panel's white into level 3 and lose those highlights.
    """
    low, high = 0.0, 1.0
    if auto_levels:
        # 0.5/99.5 rather than min/max: one stuck pixel should not decide the white point.
        low, high = (float(v) for v in np.percentile(p, (0.5, 99.5)))
        if high - low < 0.5:
            # A foggy or night source would be stretched into its own noise by a full
            # 1/span gain. Cap the stretch at 2x around the picture's own midpoint.
            mid = 0.5 * (low + high)
            low, high = max(0.0, mid - 0.25), min(1.0, mid + 0.25)
        if high <= low:
            low, high = 0.0, 1.0
    t = np.clip((p - low) / (high - low), 0.0, 1.0)
    if gamma != 1.0:
        t = t ** (1.0 / gamma)
    if contrast != 1.0:
        t = np.clip((t - 0.5) * contrast + 0.5, 0.0, 1.0)
    black = float(linear_to_srgb(np.float32(light[0])))
    white = float(linear_to_srgb(np.float32(light[-1])))
    return (black + t * (white - black)).astype(np.float32)


def to_axis(linear: np.ndarray, light: np.ndarray) -> np.ndarray:
    """Linear light to the level axis 0..3, piecewise-linear over the real ladder.

    One unit of the axis is one level, so error diffusion averages to the right light.
    """
    return np.interp(linear, light, (0.0, 1.0, 2.0, 3.0)).astype(np.float64)


def diffuse(value: np.ndarray, kernel) -> np.ndarray:
    """Serpentine error diffusion along the level axis. Slow, and worth it."""
    height, width = value.shape
    pad = 2
    work = np.zeros((height + 2 * pad, width + 2 * pad), dtype=np.float64)
    work[pad : pad + height, pad : pad + width] = value
    out = np.zeros((height, width), dtype=np.uint8)
    for y in range(height):
        forward = y % 2 == 0
        columns = range(width) if forward else range(width - 1, -1, -1)
        row = y + pad
        for x in columns:
            old = work[row, x + pad]
            new = min(3.0, max(0.0, round(old)))
            out[y, x] = int(new)
            error = old - new
            step = 1 if forward else -1
            for dy, dx, weight in kernel:
                work[row + dy, x + pad + step * dx] += error * weight
    return out


def quantise(linear: np.ndarray, dither: str, light: np.ndarray) -> np.ndarray:
    value = to_axis(linear, light)
    height, width = value.shape

    if dither == "none":
        return np.clip(np.rint(value), 0, 3).astype(np.uint8)
    if dither == "fs":
        return diffuse(value, FS_KERNEL)
    if dither == "atkinson":
        return diffuse(value, ATKINSON_KERNEL)

    if dither == "bayer4":
        mask = BAYER4
    elif dither == "bayer8":
        mask = bayer8()
    else:
        mask = blue_noise()
    tiled = np.tile(mask, (height // mask.shape[0] + 1, width // mask.shape[1] + 1))[:height, :width]
    # Ordered dithering proper: a pixel f of the way between two levels lands on the
    # higher one for exactly the fraction f of the mask's cells.
    return np.clip(np.floor(value + tiled), 0, 3).astype(np.uint8)


def pack(levels: np.ndarray) -> bytes:
    height, width = levels.shape
    bytes_per_row = (width + 3) // 4
    padded = np.zeros((height, bytes_per_row * 4), dtype=np.uint8)
    padded[:, :width] = levels
    groups = padded.reshape(height, bytes_per_row, 4).astype(np.uint8)
    packed = (groups[:, :, 0] << 6) | (groups[:, :, 1] << 4) | (groups[:, :, 2] << 2) | groups[:, :, 3]
    return struct.pack("<HH", width, height) + packed.astype(np.uint8).tobytes()


def unpack(blob: bytes) -> np.ndarray:
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


def selfcheck() -> int:
    """Encode a ramp and a padded strip: assert format, round trip and rising tone."""
    rng = np.random.default_rng(7)
    small = rng.integers(0, 4, (9, 13), dtype=np.uint8)
    blob = pack(small)
    assert struct.unpack("<HH", blob[:4]) == (13, 9), "header is not width,height LE"
    assert len(blob) == 4 + ((13 + 3) // 4) * 9, "payload is not bytesPerRow * height"
    assert np.array_equal(unpack(blob), small), "pack/unpack round trip"

    width, height = DEVICES["x4pro"]
    light = parse_levels("panel")
    ramp = np.tile(np.linspace(0.0, 1.0, width, dtype=np.float32), (height, 1))
    p = tone(linear_to_srgb(ramp), light, auto_levels=False, gamma=1.0, contrast=1.0)
    levels = quantise(srgb_to_linear(p), "fs", light)
    blob = pack(levels)
    assert len(blob) == 4 + ((width + 3) // 4) * height, "payload is not bytesPerRow * height"
    assert struct.unpack("<HH", blob[:4]) == (width, height), "header is not the panel size"

    back = unpack(blob)
    assert back.shape == (height, width), "decoded shape is not the panel"
    assert set(np.unique(back).tolist()) == {0, 1, 2, 3}, "the ramp did not reach all four levels"

    shown = light[back].astype(np.float64).mean(axis=0)
    blocks = shown.reshape(-1, 16).mean(axis=1)
    assert np.all(np.diff(blocks) >= -0.002), "tone is not monotonic across the ramp"
    target = srgb_to_linear(p[0]).astype(np.float64)
    error = float(np.abs(shown - target).mean())
    assert error < 0.02, "ramp light does not track the target"
    print(
        f"selfcheck ok: 13x9 roundtrip, {width}x{height} ramp, levels {sorted(set(np.unique(back).tolist()))}, "
        f"mean |light error| {error:.4f}"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("sources", nargs="*", type=Path, help="image files to encode")
    parser.add_argument("--device", choices=sorted(DEVICES), default="x4pro", help="panel size to author for")
    parser.add_argument("--size", help="override as WIDTHxHEIGHT")
    parser.add_argument("--out", type=Path, default=Path("."), help="output directory")
    parser.add_argument("--fit", choices=("cover", "contain"), default="cover")
    parser.add_argument("--rotate", type=int, default=0, help="degrees, counter-clockwise, before fitting")
    parser.add_argument("--dither", choices=DITHERS, default="fs")
    parser.add_argument("--levels", default="panel", help="panel, nominal, or four sRGB bytes: 15,30,80,210")
    parser.add_argument(
        "--auto-levels",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="black/white points from the 0.5th/99.5th percentiles (default on)",
    )
    parser.add_argument("--gamma", type=float, default=1.0, help="perceptual midtone gamma; >1 lightens")
    parser.add_argument("--contrast", type=float, default=1.0, help="perceptual contrast about mid grey")
    parser.add_argument("--sharpen", type=float, default=0.6, help="0 disables; applied either side of the resize")
    parser.add_argument("--matrix", action="store_true", help="write every recipe, with a MANIFEST.txt")
    parser.add_argument("--verify", action="store_true", help="decode each file back and compare")
    parser.add_argument("--selfcheck", action="store_true", help="encode a ramp and assert the result")
    args = parser.parse_args()

    if args.selfcheck:
        return selfcheck()
    if not args.sources:
        return parser.error("no source images given (or pass --selfcheck)")

    if args.size:
        try:
            width, height = (int(part) for part in args.size.lower().split("x"))
        except ValueError:
            return parser.error("--size wants WIDTHxHEIGHT, e.g. 800x480")
        size = (width, height)
    else:
        size = DEVICES[args.device]
    parse_levels(args.levels)  # fail on a bad ladder before touching any image

    args.out.mkdir(parents=True, exist_ok=True)

    if args.matrix:
        # One axis at a time, not the cross product: thirteen files a tester can step
        # through. Each axis is measured against what --matrix does by default, so the
        # last two use the old nominal ladder and read visibly too dark on the panel.
        recipes = [(d, True, 1.0, 1.0, "panel") for d in DITHERS]
        recipes += [
            ("fs", False, 1.0, 1.0, "panel"),
            ("fs", True, 1.2, 1.0, "panel"),
            ("fs", True, 0.85, 1.0, "panel"),
            ("fs", True, 1.0, 1.25, "panel"),
            ("fs", True, 1.0, 0.8, "panel"),
            ("fs", True, 1.0, 1.0, "nominal"),
            ("none", True, 1.0, 1.0, "nominal"),
        ]
    else:
        recipes = [(args.dither, args.auto_levels, args.gamma, args.contrast, args.levels)]

    manifest = []
    index = 0
    for source in args.sources:
        if not source.is_file():
            print(f"skipping {source}: not a file", file=sys.stderr)
            continue
        rgb = load_rgb(source, size, args.rotate)
        lin = linear_luma(rgb)
        scale = fit_scale(lin.shape, size, args.fit)
        if scale < 0.7:
            # Detail a 3x+ downscale would otherwise lose. Boosting the source before the
            # filter keeps microcontrast that the resample would average away.
            lin = srgb_to_linear(sharpen(linear_to_srgb(lin), args.sharpen * 0.5, passes=2))
        lin = fit_linear(lin, size, args.fit, scale)
        p = sharpen(linear_to_srgb(lin), args.sharpen)
        for dither, auto_levels, gamma, contrast, level_spec in recipes:
            light = parse_levels(level_spec)
            levels = quantise(srgb_to_linear(tone(p, light, auto_levels, gamma, contrast)), dither, light)
            if args.matrix:
                index += 1
                name = f"M{index:02d}.PXC"
            else:
                name = (source.stem.upper()[:8] or "OUT") + ".PXC"
            out_path = args.out / name
            write_pxc(out_path, levels, args.verify)
            recipe = (
                f"{name}  {source.name}  {size[0]}x{size[1]}  dither={dither}  "
                f"auto_levels={int(auto_levels)}  gamma={gamma}  contrast={contrast}  levels={level_spec}"
            )
            manifest.append(recipe)
            print(recipe)

    if args.matrix:
        (args.out / "MANIFEST.txt").write_text("\n".join(manifest) + "\n")
        print(f"\n{len(manifest)} files and MANIFEST.txt in {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
