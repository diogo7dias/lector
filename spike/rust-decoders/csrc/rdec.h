// C ABI of the Rust staticlib (../src/lib.rs).
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*rdec_row_cb)(void* ctx, int y, const uint8_t* gray, int width);

int rdec_jpeg_gray(const uint8_t* data, size_t len, rdec_row_cb cb, void* ctx, int* w, int* h);
int rdec_png_gray(const uint8_t* data, size_t len, rdec_row_cb cb, void* ctx, int* w, int* h);
int64_t rdec_inflate_raw(const uint8_t* data, size_t len, uint32_t* sum);
void rdec_heap_reset(void);
size_t rdec_heap_peak(void);

#ifdef __cplusplus
}
#endif
