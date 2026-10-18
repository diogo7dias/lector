#!/usr/bin/env python3
"""Sign lector firmware images in place.

Appends a signature block after the image: b"LSG1" + r || s, the raw ECDSA P-256
signature over the image's own SHA-256 trailer (hash_appended images only). The
device checks it in firmware_flash::validateImageStream (src/network/FirmwareSignature.*):
an online update must carry it, an SD-card install warns without it. The bootloader
ignores bytes past the image, so a signed file still flashes like any other.

Usage: FIRMWARE_SIGNING_KEY="$(cat key.pem)" sign_firmware.py firmware.bin [...]
Re-signing replaces an existing block rather than stacking a second one.
"""

import hashlib
import os
import struct
import sys

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

MAGIC = b"LSG1"
BLOCK_SIZE = len(MAGIC) + 64


def image_end(data: bytes) -> int:
    """Offset just past the SHA-256 trailer, walking the segment table like the device."""
    if len(data) < 24 or data[0] != 0xE9:
        raise ValueError("not an ESP image (bad magic)")
    if data[23] == 0:
        raise ValueError("image has no SHA-256 trailer (hash_appended=0); nothing to sign")
    pos = 24
    for i in range(data[1]):
        if pos + 8 > len(data):
            raise ValueError(f"segment {i} header runs past the end")
        (length,) = struct.unpack_from("<I", data, pos + 4)
        pos += 8 + length
    pad_end = (pos + 16) & ~15
    end = pad_end + 32
    if end > len(data):
        raise ValueError("image truncated")
    if hashlib.sha256(data[:pad_end]).digest() != data[pad_end:end]:
        raise ValueError("SHA-256 trailer does not match the image")
    return end


def sign(data: bytes, key: ec.EllipticCurvePrivateKey) -> bytes:
    end = image_end(data)
    tail = data[end:]
    if tail and tail != b"\xff" * len(tail) and not (len(tail) == BLOCK_SIZE and tail.startswith(MAGIC)):
        raise ValueError(f"{len(tail)} unexpected bytes after the image")
    digest = data[end - 32 : end]
    der = key.sign(digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
    r, s = utils.decode_dss_signature(der)
    return data[:end] + MAGIC + r.to_bytes(32, "big") + s.to_bytes(32, "big")


def main(paths) -> int:
    pem = os.environ.get("FIRMWARE_SIGNING_KEY", "")
    if not pem.strip():
        print("FIRMWARE_SIGNING_KEY is empty: refusing to ship an unsigned release", file=sys.stderr)
        return 1
    key = serialization.load_pem_private_key(pem.encode(), password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or key.curve.name != "secp256r1":
        print("FIRMWARE_SIGNING_KEY must be an EC P-256 private key", file=sys.stderr)
        return 1
    for path in paths:
        with open(path, "rb") as f:
            data = f.read()
        signed = sign(data, key)
        with open(path, "wb") as f:
            f.write(signed)
        print(f"signed {path} ({len(signed)} bytes)")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1:]))
