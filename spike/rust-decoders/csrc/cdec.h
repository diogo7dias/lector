// The C decoders the firmware ships (JPEGDEC, PNGdec, InflateStream on tinfl, InflateReader
// on uzlib), driven like ../src/lib.rs so the bench and the host check feed both the same way.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "rdec.h"

// Full-scale 8-bit gray, top to bottom by row (JPEG rows are emitted per MCU row strip).
int c_jpeg_gray(const uint8_t* data, size_t len, rdec_row_cb cb, void* ctx, int* w, int* h);
// The firmware's gray conversion and white matte (PngToFramebufferConverter convertLineToGray).
int c_png_gray(const uint8_t* data, size_t len, rdec_row_cb cb, void* ctx, int* w, int* h);
int64_t c_tinfl_raw(const uint8_t* data, size_t len, uint32_t* sum);
int64_t c_uzlib_raw(const uint8_t* data, size_t len, uint32_t* sum);
