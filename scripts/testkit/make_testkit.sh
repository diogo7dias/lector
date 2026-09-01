#!/usr/bin/env bash
# Build a Mac test kit: firmware + a double-clickable flasher and log recorder.
#
# Usage: scripts/testkit/make_testkit.sh [output_dir] [env]
#
# env defaults to "testkit" (the ESP32-C3 X3/X4 build). Pass "x4pro" for the
# X4 Pro, which is a different chip (ESP32-S3) and so needs its own kit: the
# flasher reads the chip out of kit.env, and an S3 image written with --chip
# esp32c3 is refused by the ROM loader.
#
# The kit is a zip the reader downloads, unzips, and double-clicks. It flashes
# over USB and writes a log to ~/Downloads that comes back here as the evidence
# for whatever was being tested. One binary serves both X3 and X4, so there is
# nothing to choose at flash time.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${1:-$HOME/tools/testkit-www}"
# Overridable so a one-off kit can carry a different build: positional argument
# first, then TESTKIT_ENV, so neither call style needs the script edited.
ENV_NAME="${2:-${TESTKIT_ENV:-testkit}}"
BUILD_DIR="$REPO_ROOT/.pio/build/$ENV_NAME"
# Downloaded once by hand; see docs/testkit.md. Kept out of the repo because it
# is a 13 MB prebuilt binary.
ESPTOOL_SRC="${ESPTOOL_MACOS_ARM64:-$HOME/tools/testkit-tools/esptool-macos-arm64}"

cd "$REPO_ROOT"

VERSION="$(grep -E '^version = ' platformio.ini | head -1 | cut -d= -f2- | xargs)"
COMMIT="$(git rev-parse --short HEAD)"
DIRTY=""
git diff --quiet || DIRTY="-dirty"
VERSION_SLUG="$(printf '%s' "$VERSION" | tr ' ' '-' | tr -cd '[:alnum:].-')"
STAMP="$(date +%Y%m%d-%H%M%S)"
case "$ENV_NAME" in
  x4pro) KIT_CHIP="esp32s3"; KIT_LABEL="x4pro-" ;;
  *) KIT_CHIP="esp32c3"; KIT_LABEL="" ;;
esac
KIT_NAME="lector-testkit-${KIT_LABEL}${VERSION_SLUG}-${COMMIT}${DIRTY}-${STAMP}"

echo "Building $ENV_NAME for $VERSION ($COMMIT$DIRTY)"
pio run -e "$ENV_NAME" >/dev/null

BOOT_APP0="$(find "$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions" -name boot_app0.bin | head -1)"
[ -n "$BOOT_APP0" ] || { echo "boot_app0.bin not found" >&2; exit 1; }
[ -x "$ESPTOOL_SRC" ] || { echo "esptool binary not found at $ESPTOOL_SRC" >&2; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
KIT="$STAGE/$KIT_NAME"
mkdir -p "$KIT/firmware" "$KIT/tools"

cp "$BUILD_DIR/bootloader.bin" "$BUILD_DIR/partitions.bin" "$BUILD_DIR/firmware.bin" "$KIT/firmware/"
cp "$BOOT_APP0" "$KIT/firmware/boot_app0.bin"
cp "$ESPTOOL_SRC" "$KIT/tools/esptool-macos-arm64"
cp "$REPO_ROOT/scripts/testkit/run.command" "$KIT/run.command"
cp "$REPO_ROOT/scripts/testkit/README.txt" "$KIT/README.txt"
chmod +x "$KIT/run.command" "$KIT/tools/esptool-macos-arm64"

cat > "$KIT/kit.env" <<ENVEOF
KIT_VERSION="$VERSION"
KIT_VERSION_SLUG="$VERSION_SLUG"
KIT_COMMIT="$COMMIT$DIRTY"
KIT_BUILT="$(date '+%Y-%m-%d %H:%M:%S %z')"
KIT_CHIP="$KIT_CHIP"
KIT_BAUD="115200"
KIT_FLASH_SIZE="16MB"
ENVEOF

mkdir -p "$OUT_DIR"
# Zipped through Python because the executable bit has to survive: macOS will not
# run a run.command that arrives without it, and neither will the bundled esptool.
python3 - "$STAGE" "$KIT_NAME" "$OUT_DIR/$KIT_NAME.zip" <<'PYEOF'
import os
import sys
import zipfile

stage, kit_name, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
root = os.path.join(stage, kit_name)
with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as z:
    for dirpath, _, filenames in os.walk(root):
        for name in sorted(filenames):
            full = os.path.join(dirpath, name)
            # No wrapping folder: the kit unzips straight into whatever directory
            # it is run from, so the flash command is one fixed line every time.
            rel = os.path.relpath(full, root)
            info = zipfile.ZipInfo.from_file(full, rel)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (os.stat(full).st_mode & 0xFFFF) << 16
            with open(full, "rb") as fh:
                z.writestr(info, fh.read())
PYEOF

# A stable name alongside the timestamped one, so the command handed over never
# changes between kits.
cp "$OUT_DIR/$KIT_NAME.zip" "$OUT_DIR/lector-testkit-${KIT_LABEL}latest.zip"

echo "$OUT_DIR/$KIT_NAME.zip"
du -h "$OUT_DIR/$KIT_NAME.zip" | cut -f1
