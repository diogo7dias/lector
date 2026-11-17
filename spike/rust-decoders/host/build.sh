#!/usr/bin/env bash
# Builds host/compare: the C decoders at the firmware pins with the repo patches (the bench's
# patched checkouts) and the device build flags the fuzzer uses, against the Rust lib (std).
set -euo pipefail
cd "$(dirname "$0")"
CRATE=..
REPO=../../..
DEPS=$CRATE/bench/.pio/libdeps/c3
OUT=build
mkdir -p $OUT
rm -rf $OUT/JPEGDEC && cp -r $DEPS/JPEGDEC $OUT/JPEGDEC
git -C $OUT/JPEGDEC apply "$PWD/$REPO/test/fuzz/image/jpegdec-device-register-width.patch"
cargo build --release --features std --manifest-path $CRATE/Cargo.toml --target-dir $OUT/target
P=$DEPS/PNGdec/src
FLAGS="-O2 -ffunction-sections -fdata-sections -D__LINUX__ -DPNG_MAX_BUFFERED_PIXELS=16416 -DNO_SIMD -DLECTOR_DEVICE_REGISTER_WIDTH -I$CRATE/csrc
  -I$OUT/JPEGDEC/src -I$P -I$REPO/lib/miniz/src -I$REPO/lib/uzlib/src -I$REPO/lib/InflateReader -I$REPO/lib/Memory"
for c in $P/adler32.c $P/crc32.c $P/infback.c $P/inffast.c $P/inflate.c $P/inftrees.c $P/zutil.c \
  $REPO/lib/miniz/src/miniz_impl.c $REPO/lib/uzlib/src/tinflate.c; do
  gcc $FLAGS -c "$c" -o "$OUT/$(basename "$c").o"
done
g++ $FLAGS -std=gnu++20 -fno-exceptions compare.cpp $CRATE/csrc/cdec.cpp $CRATE/csrc/BuildScratch.cpp \
  $OUT/JPEGDEC/src/JPEGDEC.cpp $P/PNGdec.cpp $REPO/lib/miniz/src/InflateStream.cpp \
  $REPO/lib/InflateReader/InflateReader.cpp $OUT/*.c.o $OUT/target/release/liblector_rdec.a -Wl,--gc-sections -lpthread -ldl -o $OUT/compare
echo "built $OUT/compare"
