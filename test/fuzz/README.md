# Fuzzing and sanitizers

The parsers that read untrusted input (EPUB zips, deflate streams, CSS, OPDS feeds, chapter
XHTML, EPUB metadata, PNG, JPEG and GIF images, OPF text) have
libFuzzer harnesses here. All host tests can also be built with ASan and UBSan.

## Sanitized host tests (GCC or clang)

```bash
python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/   # once per clone
cmake -S test -B build/test-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLECTOR_SANITIZE=ON
cmake --build build/test-asan -j4 && ctest --test-dir build/test-asan -j4 --output-on-failure
```

## Fuzzers (clang only)

```bash
CC=clang CXX=clang++ cmake -S test -B build/fuzz -G Ninja -DLECTOR_FUZZ=ON
cmake --build build/fuzz --target fuzz_zip fuzz_inflate fuzz_css fuzz_opds fuzz_chapter fuzz_metadata \
  fuzz_image fuzz_opf_text
ctest --test-dir build/fuzz -R fuzz_                     # replay every corpus once

# Fuzz one target for 10 minutes; new inputs go to a scratch dir, never the corpus.
mkdir -p /tmp/fz/new /tmp/fz/art
build/fuzz/fuzz/css/fuzz_css /tmp/fz/new test/fuzz/css/corpus -dict=test/fuzz/css/css.dict \
  -fork=2 -ignore_crashes=1 -max_total_time=600 -rss_limit_mb=1024 -artifact_prefix=/tmp/fz/art/
```

| Target | Code under test | Input |
|--------|-----------------|-------|
| `zip` | `lib/ZipFile` + `lib/miniz` inflate | a whole .epub/.zip file |
| `inflate` | `lib/InflateReader` + `lib/uzlib` | 2 mode bytes, then raw deflate |
| `css` | `lib/Epub/Epub/css/CssParser` | 1 mode byte, then CSS |
| `opds` | `lib/OpdsParser` + `lib/expat` (firmware config) | 1 chunk-size byte, then XML |
| `chapter` | `ChapterHtmlSlimParser` + layout (`ParsedText`, `Page`, `CssParser`) on expat | 4 setting bytes, CSS, NUL, chapter XHTML |
| `metadata` | container, OPF, NCX and nav parsers + `BookMetadataCache` | 1 mode byte, then 4 NUL-separated docs (or a raw book.bin) |
| `image` | `ImageDimsProbe`, `ImageDecoderFactory`, PNG/JPEG/GIF converters (scale, dither, `DirectPixelWriter`, `PixelCache`) on patched PNGdec + JPEGDEC + AnimatedGIF | 8 config bytes, then a PNG, JPEG or GIF file |
| `opf_text` | `OpfText` (description flattening, guide cover-page scan) + `utf8ComposeNfc` | cover page href, NUL, cover page XHTML |

On a crash: minimize it (`fuzz_<t> -minimize_crash=1 -runs=20000 <crash>`), fix the root
cause in `lib/` or `src/`, then add the minimized file to `<t>/corpus/` as `regress_<what>.bin`
so the ctest replay keeps it fixed. Keep corpus files small (under 16 KB).

`chapter` and `metadata` share the in-memory storage in `common/` (nothing touches the real
disk). `FUZZ_TRACE=1` prints the firmware's log lines while replaying an input, to check a
seed reaches the code it was written for; `fuzz_chapter` also prints the bytes of pages built.
Chapter seeds need a DOCTYPE for HTML entities like `&nbsp;`: without one expat rejects them,
as on the device.

`image` fetches PNGdec, JPEGDEC and AnimatedGIF at the firmware's pins (network at configure time)
and applies `scripts/pngdec_patches/`, `scripts/jpegdec_patches/` and `scripts/animatedgif_patches/`,
the patches `scripts/patch_decoders.py` applies to the firmware build. A fix inside any of the
libraries is a new patch file there. The config bytes start from `ImageBlock::render`'s call (exact box, dithered,
cached, portrait, BW) and keep its bounds check: the box always lies on screen. The harness
renders into an exact-size frame or grayscale strip, fails if any pixel outside the image box
changes, and checks a written `.pxc` against what `renderFromCache` reads. It caps nothing
itself: the converters' 8 MP / `INT16_MAX` limit and PNG row-buffer guard are the device's.
JPEGDEC is built as the device runs it, with plain C paths (`NO_SIMD`) and the 32-bit bit
reader (`jpegdec-device-register-width.patch`); AnimatedGIF likewise at 32-bit register width
(`animatedgif-device-register-width.patch`) and `MAX_WIDTH=3072`. All three libraries load words from unaligned
addresses on purpose (`ALLOWS_UNALIGNED`), so UBSan's alignment check is off in their units
only. Not covered: the S3's `s3_simd_*.S` JPEG paths.
