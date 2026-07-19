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
bool fshelpers_check_file_extension(const uint8_t* name_ptr, size_t name_len,
                                    const uint8_t* ext_ptr, size_t ext_len);

// Numeric-aware, case-insensitive natural comparison; true when a orders before
// b. Null pointers read as empty. Byte-exact mirror of C++ FsHelpers::naturalLess.
bool fshelpers_natural_less(const uint8_t* a_ptr, size_t a_len, const uint8_t* b_ptr,
                            size_t b_len);

#ifdef __cplusplus
}  // extern "C"
#endif
