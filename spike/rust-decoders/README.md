# Rust decoder spike

Measurement only. This directory checks whether pure-Rust decoders could replace the C image and
deflate decoders that the firmware ships. Nothing here is wired into `platformio.ini` or the
firmware build.

| C decoder (firmware pin and patches) | Rust candidate |
| --- | --- |
| JPEGDEC 86282979 + `scripts/jpegdec_patches` | zune-jpeg 0.5.15 |
| PNGdec 1.1.6 (9a9c585f) + `scripts/pngdec_patches`, with its bundled zlib | zune-png 0.5.2 (inflates with zune-inflate 0.2.54) |
| tinfl (`lib/miniz`, through `InflateStream`) and uzlib (`lib/uzlib`, through `InflateReader`) | miniz_oxide 0.9.1 |

The `png` crate (0.18) is not a candidate. It has no `std` feature to turn off and reads through
`std::io`, so it does not build `no_std`.

## Layout

- `src/lib.rs` is the Rust staticlib. Its C ABI is in `csrc/rdec.h`: decode a JPEG or PNG to 8-bit
  gray rows through a callback, and inflate a raw deflate stream. A counting global allocator
  reports the peak heap.
- `csrc/cdec.cpp` drives the C decoders through the same callback. JPEGDEC runs in
  `EIGHT_BIT_GRAYSCALE`. PNG uses the firmware's `convertLineToGray`. Deflate goes through the
  firmware's own `InflateStream` and `InflateReader`.
- `bench/` is a standalone PlatformIO project for the C3 and S3 (Arduino, the firmware's platform
  pin). It embeds the test images, times every decode under the lector QEMU with `-icount`, and
  counts heap through `--wrap=malloc,calloc,realloc,free`. It also records each task's stack
  high-water mark.
- `host/` builds the same C and Rust code for x86-64. It is used for the full-frame correctness
  check and for the regression inputs.

## Run

```bash
./sim/build-qemu.sh                       # once, from the repo root
cd spike/rust-decoders/bench
pio run -e c3 && python3 run.py c3        # results/c3.txt
pio run -e s3 && python3 run.py s3        # results/s3.txt
python3 summary.py c3                     # paired C/Rust time, heap, stack, failed runs
./sizes.sh                                # flash deltas (builds without each side)
python3 flash.py c3                       # per-library flash from the linker map
cd ../host && ./build.sh                  # needs bench/.pio/libdeps/c3 from a bench build
build/compare <image>                     # C vs Rust frame difference
./regress.sh                              # Rust on test/fuzz/image/corpus/regress_*
```

Rust needs `rustup target add riscv32imc-unknown-none-elf` for the C3 and `espup install` (the
`esp` toolchain, `-Zbuild-std=core,alloc`) for the S3.

The bench uses the stock Arduino framework without the firmware's `custom_sdkconfig`. The first
bench build therefore reinstalls the shared framework package, and the next firmware build
rebuilds it.

## How to read the numbers

- The time is QEMU virtual time under `-icount shift=3`, so one guest instruction is 8 ns. The
  same loop gives the same count on every run (`CALIB` lines). Compare C with Rust on one target
  only. QEMU does not model caches or PSRAM latency, so the S3 numbers with buffers in PSRAM
  favour the decoder that buffers more.
- C is built at the platform's `-Os` without LTO. Rust is built at `opt-level = "s"` with LTO
  and `panic = "abort"`. Both are linked with `--gc-sections`.
- JPEGDEC on the S3 links its ESP32-S3 SIMD colour converters, as the `x4pro` firmware does. The
  EPUB JPEGs are single-channel gray, so those converters do not run.
- The decode is at full scale on both sides. The firmware asks JPEGDEC for 1/2 to 1/8 scale on
  large images, and zune-jpeg has no scaled decode. JPEGDEC decodes progressive files DC-only at
  1/8, while zune-jpeg decodes them in full. The progressive timings therefore do not compare.
- Heap is the peak of live malloc bytes during one decode. It counts operator new and the Rust
  allocator. The images sit in flash on both sides. On the device, zune needs the whole file in
  RAM as well, because its `no_std` reader takes only a slice. The firmware streams the file
  from the SD card.

## Results

See the "Measured" part of the Rust case ledger on the lector Notion project page.
