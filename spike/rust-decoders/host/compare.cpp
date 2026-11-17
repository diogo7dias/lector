// Host check: decodes one image with the C decoder and the Rust one into full gray frames and
// prints how far apart they are. `compare --rust-only FILE` runs only the Rust side, so a
// caller can tell an error (exit 2), a panic (abort) and a hang (timeout) apart.
// Corpus seeds (.bin) carry 8 harness bytes before the image; they are skipped.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "cdec.h"

namespace {

struct Frame {
  int w = 0, h = 0;
  std::vector<uint8_t> px;
};

using DecodeFn = int (*)(const uint8_t*, size_t, rdec_row_cb, void*, int*, int*);

int decode(DecodeFn fn, const std::vector<uint8_t>& in, Frame& f) {
  return fn(
      in.data(), in.size(),
      [](void* ctx, int y, const uint8_t* g, int width) {
        auto& px = static_cast<Frame*>(ctx)->px;
        if (static_cast<size_t>(y + 1) * width > px.size()) px.resize(static_cast<size_t>(y + 1) * width);
        memcpy(&px[static_cast<size_t>(y) * width], g, width);
      },
      &f, &f.w, &f.h);
}

// Box-average a frame down by 8, as JPEGDEC's DC-only progressive output is.
Frame eighth(const Frame& f) {
  Frame o;
  o.w = (f.w + 7) / 8, o.h = (f.h + 7) / 8;
  o.px.assign(static_cast<size_t>(o.w) * o.h, 0);
  for (int y = 0; y < o.h; y++)
    for (int x = 0; x < o.w; x++) {
      int sum = 0, n = 0;
      for (int dy = 0; dy < 8 && y * 8 + dy < f.h; dy++)
        for (int dx = 0; dx < 8 && x * 8 + dx < f.w; dx++, n++) sum += f.px[(y * 8 + dy) * f.w + x * 8 + dx];
      o.px[y * o.w + x] = static_cast<uint8_t>((sum + n / 2) / n);
    }
  return o;
}

bool progressive(const std::vector<uint8_t>& d) {
  for (size_t i = 2; i + 4 < d.size() && d[i] == 0xFF;) {
    if (d[i + 1] == 0xC2) return true;
    if (d[i + 1] == 0xC0 || d[i + 1] == 0xC1 || d[i + 1] == 0xDA) return false;
    i += 2 + (d[i + 2] << 8 | d[i + 3]);
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  const bool rustOnly = argc == 3 && !strcmp(argv[1], "--rust-only");
  const std::string path = argv[argc - 1];
  std::ifstream file(path, std::ios::binary);
  std::vector<uint8_t> in((std::istreambuf_iterator<char>(file)), {});
  if (path.size() > 4 && path.substr(path.size() - 4) == ".bin") in.erase(in.begin(), in.begin() + 8);
  if (argc == 3 && !strcmp(argv[1], "--inflate")) {
    uint32_t s1 = 0, s2 = 0, s3 = 0;
    const long long a = c_tinfl_raw(in.data(), in.size(), &s1);
    const long long b = rdec_inflate_raw(in.data(), in.size(), &s2);
    printf("tinfl %lld/%u miniz_oxide %lld/%u ", a, s1, b, s2);
    fflush(stdout);
    const long long u = c_uzlib_raw(in.data(), in.size(), &s3);
    printf("uzlib %lld/%u\n", u, s3);
    return 0;
  }
  const bool png = !in.empty() && in[0] == 0x89;
  Frame r, c;
  const int rr = decode(png ? rdec_png_gray : rdec_jpeg_gray, in, r);
  if (rustOnly) {
    printf("rust rc=%d %dx%d\n", rr, r.w, r.h);
    return rr == 0 ? 0 : 2;
  }
  const int cr = decode(png ? c_png_gray : c_jpeg_gray, in, c);
  const bool prog = !png && progressive(in);
  const Frame rc = prog && rr == 0 ? eighth(r) : r;
  printf("%s C rc=%d %dx%d | Rust rc=%d %dx%d", path.c_str(), cr, c.w, c.h, rr, r.w, r.h);
  if (cr == 0 && rr == 0 && rc.w == c.w && rc.h == c.h && rc.px.size() >= c.px.size()) {
    long total = 0;
    int maxd = 0, over = 0;
    for (size_t i = 0; i < c.px.size(); i++) {
      const int d = abs(rc.px[i] - c.px[i]);
      total += d;
      maxd = d > maxd ? d : maxd;
      over += d > 2;
    }
    if (getenv("DUMP")) {
      for (const auto& [name, f] : {std::pair<const char*, const Frame*>{"c.pgm", &c}, {"rust.pgm", &rc}}) {
        FILE* out = fopen((std::string(getenv("DUMP")) + name).c_str(), "wb");
        fprintf(out, "P5 %d %d 255\n", f->w, f->h);
        fwrite(f->px.data(), 1, static_cast<size_t>(f->w) * f->h, out);
        fclose(out);
      }
    }
    printf(" | %s max=%d mean=%.3f over2=%.2f%%\n", prog ? "vs 1/8 box" : "full", maxd,
           static_cast<double>(total) / c.px.size(), 100.0 * over / c.px.size());
  } else {
    printf(" | not comparable\n");
  }
  return 0;
}
