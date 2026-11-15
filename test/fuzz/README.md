# Fuzzing and sanitizers

The parsers that read untrusted input (EPUB zips, deflate streams, CSS, OPDS feeds) have
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
cmake --build build/fuzz --target fuzz_zip fuzz_inflate fuzz_css fuzz_opds
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

On a crash: minimize it (`fuzz_<t> -minimize_crash=1 -runs=20000 <crash>`), fix the root
cause in `lib/` or `src/`, then add the minimized file to `<t>/corpus/` as `regress_<what>.bin`
so the ctest replay keeps it fixed. Keep corpus files small (under 16 KB).
