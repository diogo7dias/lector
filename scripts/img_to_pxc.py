#!/usr/bin/env python3
"""Encode ordinary images into Lector .pxc sleep wallpapers (480x800 by default).

A .pxc is the device's packed sleep-wallpaper format (see PxcSleepRenderer.cpp /
ImageBlock.cpp on device):
  bytes 0-1  width   (uint16 LE)
  bytes 2-3  height  (uint16 LE)
  body       2 bits/pixel, MSB-first, stride = (width + 3) // 4 bytes/row
  pixel level 0..3 maps to gray {0, 85, 170, 255} (0 = black, 3 = white)

The quantiser thresholds (>=212->3, >=127->2, >=42->1, else 0) and the Floyd-
Steinberg weights (7/3/5/1) match the device decoder exactly, so what this
writes is what the panel shows.

Copy the resulting .pxc files into /sleep on the SD card, set Settings ->
Sleep wallpaper format = PXC, and they enter the rotation.

    python3 img_to_pxc.py photo.jpg
    python3 img_to_pxc.py ~/wallpapers -o ~/sd/sleep --mode cover
    python3 img_to_pxc.py art.png --resample nearest --preview

The .pxc size guard on device is +/-1 px vs the panel, so the target must match
your screen. Default 480x800 = Lector/CrossPoint. Override with --to for other
panels (e.g. --to 528x792 for an X3).
"""

import argparse
import os
import sys

import numpy as np
from PIL import Image, ImageOps

IMAGE_EXTS = (".png", ".jpg", ".jpeg", ".bmp", ".gif", ".webp", ".tif", ".tiff")
PALETTE_GRAY = [0, 85, 170, 255]  # 2-bit level -> gray, matches the device


def load_gray(path):
    """Load any image as a grayscale float32 HxW array (0..255), EXIF-oriented."""
    img = Image.open(path)
    img = ImageOps.exif_transpose(img)  # honour phone-photo rotation
    img = img.convert("L")
    return np.asarray(img, dtype=np.float32)


def _resize_gray(gray, tw, th, mode, pad, resample):
    """Resample a gray HxW array to (th, tw) using the chosen fit mode."""
    src = Image.fromarray(np.clip(gray, 0, 255).astype(np.uint8), "L")
    sw, sh = src.width, src.height
    bg = 255 if pad == "white" else 0
    if mode == "pad":
        # 1:1, centred; pad the smaller axis, centre-crop the larger. No resample.
        canvas = Image.new("L", (tw, th), bg)
        left, top = (tw - sw) // 2, (th - sh) // 2
        cl, ct = max(0, -left), max(0, -top)
        cropped = src.crop((cl, ct, cl + min(sw, tw), ct + min(sh, th)))
        canvas.paste(cropped, (max(0, left), max(0, top)))
        return canvas
    if mode == "stretch":
        return src.resize((tw, th), resample)
    if mode == "cover":
        scale = max(tw / sw, th / sh)
        nw, nh = max(1, round(sw * scale)), max(1, round(sh * scale))
        r = src.resize((nw, nh), resample)
        left = (nw - tw) // 2
        top = (nh - th) // 2
        return r.crop((left, top, left + tw, top + th))
    # fit / contain: scale to fit, pad the remainder
    scale = min(tw / sw, th / sh)
    nw, nh = max(1, round(sw * scale)), max(1, round(sh * scale))
    r = src.resize((nw, nh), resample)
    canvas = Image.new("L", (tw, th), bg)
    canvas.paste(r, ((tw - nw) // 2, (th - nh) // 2))
    return canvas


def _quantize_none(g):
    """Threshold-quantise gray -> level index 0..3, matching the device encoder
    (>=212->3, >=127->2, >=42->1, else 0)."""
    idx = np.zeros(g.shape, dtype=np.uint8)
    idx[g >= 42] = 1
    idx[g >= 127] = 2
    idx[g >= 212] = 3
    return idx


def _dither_to_levels(limg, dither):
    """Quantise an 'L' image to level indices 0..3 (device palette 0/85/170/255).

    Explicit device-matching thresholds (not PIL's palette quantiser, which
    corrupts line art). Floyd-Steinberg error diffusion with encoder weights
    7/3/5/1 when dither is enabled; exact threshold quantisation otherwise."""
    g = np.asarray(limg, dtype=np.int16)
    if dither == Image.Dither.NONE:
        return _quantize_none(g)
    buf = g.astype(np.float32)
    h, w = buf.shape
    idx = np.zeros((h, w), dtype=np.uint8)
    levels = np.array([0.0, 85.0, 170.0, 255.0], dtype=np.float32)
    for y in range(h):
        row = buf[y]
        for x in range(w):
            old = row[x]
            q = 3 if old >= 212 else 2 if old >= 127 else 1 if old >= 42 else 0
            idx[y, x] = q
            err = old - levels[q]
            if x + 1 < w:
                row[x + 1] += err * 7 / 16
            if y + 1 < h:
                nxt = buf[y + 1]
                if x > 0:
                    nxt[x - 1] += err * 3 / 16
                nxt[x] += err * 5 / 16
                if x + 1 < w:
                    nxt[x + 1] += err * 1 / 16
    return idx


def encode_pxc(gray, tw, th, mode, pad, resample, dither, invert=False):
    """Return (.pxc bytes, level-index array) for a gray HxW array fitted to tw x th.

    invert=True flips the 2-bit level (level' = 3 - level) for panels with
    inverted grayscale polarity. Lector (480x800, SSD1677-style) does NOT need
    it; kept for other panels (e.g. the X3 UC81xx controller)."""
    limg = _resize_gray(gray, tw, th, mode, pad, resample)
    idx = _dither_to_levels(limg, dither)  # (th, tw) indices 0..3
    if invert:
        idx = 3 - idx
    stride = (tw + 3) // 4
    padw = stride * 4
    if padw > tw:
        idx = np.pad(idx, ((0, 0), (0, padw - tw)))
    packed = (idx[:, 0::4] << 6) | (idx[:, 1::4] << 4) | (idx[:, 2::4] << 2) | idx[:, 3::4]
    out = bytearray(4 + stride * th)
    out[0] = tw & 0xFF
    out[1] = (tw >> 8) & 0xFF
    out[2] = th & 0xFF
    out[3] = (th >> 8) & 0xFF
    out[4:] = packed.astype(np.uint8).tobytes()
    return bytes(out), idx


def iter_images(inputs, recursive):
    for inp in inputs:
        if os.path.isfile(inp):
            if inp.lower().endswith(IMAGE_EXTS):
                yield inp
        elif os.path.isdir(inp):
            if recursive:
                for root, _, files in os.walk(inp):
                    for f in sorted(files):
                        if f.lower().endswith(IMAGE_EXTS):
                            yield os.path.join(root, f)
            else:
                for f in sorted(os.listdir(inp)):
                    if f.lower().endswith(IMAGE_EXTS):
                        yield os.path.join(inp, f)


def main():
    ap = argparse.ArgumentParser(description="Encode images into Lector .pxc sleep wallpapers.")
    ap.add_argument("inputs", nargs="+", help="one or more image files or directories")
    ap.add_argument("-o", "--out", help="output directory (default: alongside each source)")
    ap.add_argument("--to", default="480x800", help="target WxH (default 480x800 = Lector/CrossPoint)")
    ap.add_argument("--mode", default="cover", choices=["cover", "fit", "stretch", "pad"],
                    help="cover=fill+crop (default), fit=letterbox, stretch=distort, pad=1:1 centre")
    ap.add_argument("--invert", action="store_true",
                    help="flip black/white (level 3-n) for inverted-polarity panels (not Lector)")
    ap.add_argument("--pad", default="white", choices=["white", "black"], help="fit/pad-mode background")
    ap.add_argument("--resample", default="smooth", choices=["smooth", "nearest"],
                    help="smooth=Lanczos+Floyd-Steinberg (default, best for photos); "
                         "nearest=no dither, hard thresholds (best for flat/line art)")
    ap.add_argument("--recursive", action="store_true", help="recurse into directories")
    ap.add_argument("--preview", action="store_true",
                    help="also write <name>.preview.png of the quantised result")
    ap.add_argument("--overwrite", action="store_true", help="overwrite existing .pxc outputs")
    args = ap.parse_args()

    tw, th = (int(x) for x in args.to.lower().split("x"))
    if args.resample == "nearest":
        resample, dither = Image.NEAREST, Image.Dither.NONE
    else:
        resample, dither = Image.LANCZOS, Image.Dither.FLOYDSTEINBERG

    n_ok = n_skip = n_err = 0
    for path in iter_images(args.inputs, args.recursive):
        try:
            gray = load_gray(path)
        except Exception as e:  # noqa: BLE001 - report and continue the batch
            print(f"  ERR  {os.path.basename(path)}: {e}", file=sys.stderr)
            n_err += 1
            continue

        out_bytes, idx = encode_pxc(gray, tw, th, args.mode, args.pad, resample, dither, args.invert)

        outdir = args.out or os.path.dirname(path) or "."
        os.makedirs(outdir, exist_ok=True)
        stem = os.path.splitext(os.path.basename(path))[0]
        outpath = os.path.join(outdir, stem + ".pxc")
        if os.path.exists(outpath) and not args.overwrite:
            print(f"  skip {os.path.basename(path)}: {stem}.pxc exists (use --overwrite)")
            n_skip += 1
            continue
        with open(outpath, "wb") as f:
            f.write(out_bytes)

        if args.preview:
            Image.fromarray((idx[:, :tw] * 85).astype(np.uint8), "L").save(
                os.path.join(outdir, stem + ".preview.png"))

        n_ok += 1
        print(f"   ok  {os.path.basename(path)}  -> {stem}.pxc  {tw}x{th} ({args.mode})")

    print(f"\ndone: {n_ok} written, {n_skip} skipped, {n_err} errors -> "
          f"{args.out or '(alongside sources)'}")


if __name__ == "__main__":
    main()
