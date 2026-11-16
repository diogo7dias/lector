// Runs every embedded case through the C decoder and the Rust one, each in a fresh task, and
// prints one RESULT line per run: time under QEMU -icount, peak heap, stack used, pixel sum.
// A Rust panic (an allocation failure included) prints PANIC and restarts; any crash or panic
// leaves a START with no RESULT, and the next boot resumes after that run.
#include <Arduino.h>
#include <esp_cpu.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "cdec.h"
#include "images.h"

// Every malloc/calloc/realloc/free (operator new and the Rust allocator included) is counted
// while a run is live, so both sides' peaks come from one meter.
namespace {
volatile bool gCount = false;
int32_t gCur = 0, gPeak = 0;
void track(void* p, int sign) {
  if (!p || !gCount) return;
  gCur += sign * static_cast<int32_t>(heap_caps_get_allocated_size(p));
  if (gCur > gPeak) gPeak = gCur;
}
}  // namespace

extern "C" {
void* __real_malloc(size_t);
void* __real_calloc(size_t, size_t);
void* __real_realloc(void*, size_t);
void __real_free(void*);
void* __wrap_malloc(size_t n) {
  void* p = __real_malloc(n);
  track(p, 1);
  return p;
}
void* __wrap_calloc(size_t n, size_t m) {
  void* p = __real_calloc(n, m);
  track(p, 1);
  return p;
}
void* __wrap_realloc(void* old, size_t n) {
  track(old, -1);
  void* p = __real_realloc(old, n);
  track(p ? p : old, 1);
  return p;
}
void __wrap_free(void* p) {
  track(p, -1);
  __real_free(p);
}
}

namespace {

constexpr uint32_t kMagic = 0x52444543;
constexpr int kCaseCount = sizeof(kCases) / sizeof(kCases[0]);
// Task stack per run, from the high-water marks of a first pass at 96 KB plus a margin.
uint32_t stackBytes(const BenchCase& c, int dec) {
  if (dec != 2) return 8 * 1024;
  return c.kind == 'J' ? 56 * 1024 : c.kind == 'P' ? 32 * 1024 : 16 * 1024;
}

RTC_NOINIT_ATTR uint32_t gMagic;
RTC_NOINIT_ATTR int gNext;  // run index to start at after a restart
int gRun = 0;

enum Dec { C_MAIN, C_ALT, RUST };

struct Job {
  const BenchCase* c;
  Dec dec;
  int rc, w, h;
  uint32_t sum, rows;
  int64_t us;
  uint32_t cycles;
  int32_t peak;
  size_t rustPeak;
  uint32_t stackUsed;
  uint32_t stackBytes;
  TaskHandle_t parent;
};

void sinkRow(void* ctx, int, const uint8_t* gray, int width) {
  auto* j = static_cast<Job*>(ctx);
  for (int i = 0; i < width; i++) j->sum += gray[i];
  j->rows++;
}

// Delta builds for flash totals: -DBENCH_WITHOUT_RUST or -DBENCH_WITHOUT_C drops one side's
// calls, so the linker drops its code, tables and strings.
#if defined(BENCH_WITHOUT_RUST)
#define rdec_inflate_raw(...) (-9)
#define rdec_jpeg_gray nullptr
#define rdec_png_gray nullptr
#elif defined(BENCH_WITHOUT_C)
#define c_tinfl_raw(...) (-9)
#define c_uzlib_raw(...) (-9)
#define c_jpeg_gray nullptr
#define c_png_gray nullptr
#endif

void work(void* arg) {
  auto* j = static_cast<Job*>(arg);
  const BenchCase& c = *j->c;
  rdec_heap_reset();
  const size_t base = rdec_heap_peak();
  gCur = gPeak = 0;
  gCount = true;
  const int64_t t0 = esp_timer_get_time();
  const uint32_t k0 = esp_cpu_get_cycle_count();
  if (c.kind == 'D') {
    int64_t n = j->dec == RUST     ? rdec_inflate_raw(c.data, c.len, &j->sum)
                : j->dec == C_MAIN ? c_tinfl_raw(c.data, c.len, &j->sum)
                                   : c_uzlib_raw(c.data, c.len, &j->sum);
    j->rc = n < 0 ? static_cast<int>(n) : 0;
    j->w = n < 0 ? 0 : static_cast<int>(n);
    j->h = 1;
  } else if (c.kind == 'J') {
    const auto fn = j->dec == RUST ? rdec_jpeg_gray : c_jpeg_gray;
    j->rc = fn ? fn(c.data, c.len, sinkRow, j, &j->w, &j->h) : -9;
  } else {
    const auto fn = j->dec == RUST ? rdec_png_gray : c_png_gray;
    j->rc = fn ? fn(c.data, c.len, sinkRow, j, &j->w, &j->h) : -9;
  }
  j->cycles = esp_cpu_get_cycle_count() - k0;
  j->us = esp_timer_get_time() - t0;
  gCount = false;
  j->peak = gPeak;
  j->rustPeak = rdec_heap_peak() - base;
  j->stackUsed = j->stackBytes - uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);
  xTaskNotifyGive(j->parent);
  vTaskDelete(nullptr);
}

const char* decName(const BenchCase& c, Dec d) {
  if (d == RUST) return c.kind == 'D' ? "miniz_oxide" : c.kind == 'J' ? "zune-jpeg" : "zune-png";
  if (c.kind == 'D') return d == C_MAIN ? "tinfl" : "uzlib";
  return c.kind == 'J' ? "JPEGDEC" : "PNGdec";
}

void runOne(const BenchCase& c, Dec d) {
  Job j{&c, d, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, stackBytes(c, d), xTaskGetCurrentTaskHandle()};
  gMagic = kMagic;
  gNext = gRun + 1;
  printf("START|%d|%s|%s|stack=%u|free=%u|largest=%u\n", gRun, c.name, decName(c, d),
         static_cast<unsigned>(j.stackBytes), static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
         static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
  fflush(stdout);
  if (xTaskCreate(work, "bench", j.stackBytes / sizeof(StackType_t), &j, 5, nullptr) != pdPASS) {
    printf("NOTASK|%d\n", gRun);
    return;
  }
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  vTaskDelay(pdMS_TO_TICKS(20));  // the idle task frees the deleted task's stack
  printf("RESULT|%s|%c|%s|%d|%d|%d|%u|%lld|%u|%d|%u|%u|%u\n", c.name, c.kind, decName(c, d), j.rc, j.w, j.h,
         static_cast<unsigned>(c.len), static_cast<long long>(j.us), static_cast<unsigned>(j.cycles),
         static_cast<int>(j.peak), static_cast<unsigned>(j.rustPeak), static_cast<unsigned>(j.stackUsed),
         static_cast<unsigned>(j.sum));
  fflush(stdout);
}

// Fixed work for the time-to-instruction factor: the same loop must cost the same every run.
void calibrate() {
  volatile uint32_t x = 0;
  const int64_t t0 = esp_timer_get_time();
  const uint32_t k0 = esp_cpu_get_cycle_count();
  for (uint32_t i = 0; i < 1000000; i++) x = x + i;
  printf("CALIB|%lld|%u\n", static_cast<long long>(esp_timer_get_time() - t0),
         static_cast<unsigned>(esp_cpu_get_cycle_count() - k0));
}

}  // namespace

extern "C" [[noreturn]] void rdec_on_panic(const char* msg) {
  printf("PANIC|%d|%s\n", gRun, msg);
  fflush(stdout);
  esp_restart();
}

void setup() {
  const int start = (gMagic == kMagic) ? gNext : 0;
  gMagic = 0;
  printf("BENCH|%s|cases=%d|start=%d|psram=%u|heap=%u\n", CONFIG_IDF_TARGET, kCaseCount, start,
         static_cast<unsigned>(ESP.getPsramSize()), static_cast<unsigned>(ESP.getFreeHeap()));
  if (start == 0) {
    calibrate();
    calibrate();
  }
  for (int i = 0; i < kCaseCount; i++) {
    const BenchCase& c = kCases[i];
#ifdef BENCH_ONLY_KIND  // e.g. PLATFORMIO_BUILD_FLAGS=-DBENCH_ONLY_KIND=68 (ASCII D) to rerun one kind
    if (c.kind != BENCH_ONLY_KIND) continue;
#endif
    const Dec order[] = {C_MAIN, RUST, C_ALT};
    for (int k = 0; k < (c.kind == 'D' ? 3 : 2); k++, gRun++) {
      if (gRun < start) continue;
      runOne(c, order[k]);
    }
  }
  gMagic = 0;
  printf("DONE\n");
  fflush(stdout);
}

void loop() { vTaskDelay(portMAX_DELAY); }
