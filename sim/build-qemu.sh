#!/usr/bin/env bash
# Build Espressif's QEMU with the lector simulator patches (qemu-patches/).
# Output: sim/.qemu/build/qemu-system-riscv32 (X4, X3) and qemu-system-xtensa (X4 Pro).
# Re-run after the patches change.
set -euo pipefail
cd "$(dirname "$0")"
TAG=esp-develop-9.2.2-20260417
SRC=.qemu
C='\033[36m'; G='\033[32m'; N='\033[0m'
echo -e "${C}>> lector sim :: qemu ${TAG}${N}"
if [ ! -d "$SRC/.git" ]; then
  git clone -q --depth 1 -b "$TAG" https://github.com/espressif/qemu.git "$SRC"
fi
git -C "$SRC" checkout -q -f "$TAG"
git -C "$SRC" clean -qfd -e build
git -C "$SRC" -c user.name=sim -c user.email=sim@localhost am -q "$PWD"/qemu-patches/*.patch
mkdir -p "$SRC/build"
cd "$SRC/build"
TARGETS=riscv32-softmmu,xtensa-softmmu
[ "$(cat .targets 2>/dev/null)" = "$TARGETS" ] || { ../configure --target-list="$TARGETS" --enable-gcrypt \
  --enable-slirp --disable-werror --disable-docs --disable-sdl --disable-gtk >/dev/null && echo "$TARGETS" > .targets; }
ninja -j"$(nproc)" qemu-system-riscv32 qemu-system-xtensa >/dev/null
echo -e "${G}>> built $(pwd)/qemu-system-riscv32 and qemu-system-xtensa${N}"
