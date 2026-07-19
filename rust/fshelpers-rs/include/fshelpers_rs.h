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

#ifdef __cplusplus
}  // extern "C"
#endif
