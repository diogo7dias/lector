// FFI surface for the Rust `fshelpers_rs` staticlib.
// The C++ firmware links against libfshelpers_rs.a (built by tools/build_rust.py).
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Case-insensitive (ASCII) check that the first `name_len` bytes at `name_ptr`
// end with the first `ext_len` bytes at `ext_ptr`. Null pointers are treated as
// empty. Byte-exact mirror of C++ FsHelpers::checkFileExtension.
bool fshelpers_check_file_extension(const uint8_t* name_ptr, size_t name_len, const uint8_t* ext_ptr, size_t ext_len);

// Numeric-aware, case-insensitive natural comparison; true when a orders before
// b. Null pointers read as empty. Byte-exact mirror of C++ FsHelpers::naturalLess.
bool fshelpers_natural_less(const uint8_t* a_ptr, size_t a_len, const uint8_t* b_ptr, size_t b_len);

// Directories-first natural ordering; a name ending in '/' sorts before any file.
// Byte-exact mirror of C++ FsHelpers::naturalFileLess.
bool fshelpers_natural_file_less(const uint8_t* a_ptr, size_t a_len, const uint8_t* b_ptr, size_t b_len);

// Sanitize a filename into the caller buffer at out_ptr (capacity out_cap);
// returns the result length. Byte-exact mirror of C++ StringUtils::sanitizeFilename.
// Caller should size out_cap to at least max(max_bytes, 4).
size_t fshelpers_sanitize_filename(const uint8_t* name_ptr, size_t name_len, size_t max_bytes, uint8_t* out_ptr,
                                   size_t out_cap);

// Reverse one PNG scanline filter (0=None 1=Sub 2=Up 3=Average 4=Paeth) in place.
// cur is the filtered row (reconstructed on return); prev is the row above (may be
// null/empty for the top row); bpp is the byte step to the left pixel. Returns
// false on an unknown filter or bpp==0. Bounds-checked (safe on malformed images).
bool fshelpers_png_unfilter_row(uint8_t filter, uint8_t* cur_ptr, size_t cur_len, const uint8_t* prev_ptr,
                                size_t prev_len, size_t bpp);

// Unpack one BMP pixel row (bpp 32/24/8/4/2/1) into `width` 8-bit luminance bytes
// at out_ptr (capacity out_cap, needs `width`). row is the raw pixel row; palette
// is the 256-entry paletteLum table (may be null/empty for 24/32-bit). Returns
// false on unsupported bpp, a too-short row/out, or a palette < 256 for paletted
// depths. Byte-exact mirror of the switch(bpp) unpack in Bitmap::readNextRow;
// bounds-checked (safe on malformed images).
bool fshelpers_bmp_unpack_row(const uint8_t* row_ptr, size_t row_len, uint16_t bpp, size_t width,
                              const uint8_t* palette_ptr, size_t palette_len, uint8_t* out_ptr, size_t out_cap);

#ifdef __cplusplus
}  // extern "C"
#endif
