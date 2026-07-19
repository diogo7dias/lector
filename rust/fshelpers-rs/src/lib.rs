// fshelpers-rs: memory-safe reimplementations of crash-prone C++ FsHelpers routines.
//
// no_std for the device build (feature = "device"); std otherwise so the host
// harness (`cargo test`) can exercise the pure logic below.
#![cfg_attr(feature = "device", no_std)]

/// Case-insensitive (ASCII) check that `name` ends with `ext`.
///
/// Byte-exact mirror of C++ `FsHelpers::checkFileExtension`: compares the tail
/// `ext.len()` bytes, folding only ASCII A-Z. Returns `false` when `name` is
/// shorter than `ext` (no underflow), and `true` when `ext` is empty.
pub fn check_file_extension(name: &[u8], ext: &[u8]) -> bool {
    // Guard first: no underflow when the name is shorter than the extension.
    if name.len() < ext.len() {
        return false;
    }
    let tail = &name[name.len() - ext.len()..];
    tail.eq_ignore_ascii_case(ext)
}

/// FFI entry point. C++ passes `(ptr, len)` for both name and extension.
/// Null pointers are treated as empty slices so a bad caller cannot deref null.
///
/// # Safety
/// `name_ptr`/`ext_ptr` must each point to at least `name_len`/`ext_len` bytes,
/// or be null (which is read as an empty slice).
#[no_mangle]
pub unsafe extern "C" fn fshelpers_check_file_extension(
    name_ptr: *const u8,
    name_len: usize,
    ext_ptr: *const u8,
    ext_len: usize,
) -> bool {
    let name = slice_or_empty(name_ptr, name_len);
    let ext = slice_or_empty(ext_ptr, ext_len);
    check_file_extension(name, ext)
}

#[inline]
unsafe fn slice_or_empty<'a>(ptr: *const u8, len: usize) -> &'a [u8] {
    if ptr.is_null() || len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(ptr, len)
    }
}

// no_std device build needs its own panic handler. Host build uses std's.
#[cfg(feature = "device")]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}

#[cfg(test)]
mod tests {
    use super::check_file_extension as ext;

    #[test]
    fn matches_same_case() {
        assert!(ext(b"photo.jpg", b".jpg"));
    }

    #[test]
    fn matches_case_insensitive() {
        assert!(ext(b"PHOTO.JPG", b".jpg"));
        assert!(ext(b"photo.JpG", b".jpg"));
    }

    #[test]
    fn rejects_wrong_extension() {
        assert!(!ext(b"photo.png", b".jpg"));
    }

    #[test]
    fn name_shorter_than_ext_is_false_no_underflow() {
        // C++ would compute name.len()-ext.len(); Rust must guard, not underflow.
        assert!(!ext(b"jpg", b".jpg"));
        assert!(!ext(b"", b".jpg"));
    }

    #[test]
    fn empty_ext_is_true() {
        // Parity with C++: extLen 0 -> loop runs zero times -> true.
        assert!(ext(b"anything", b""));
        assert!(ext(b"", b""));
    }

    #[test]
    fn whole_name_equals_ext() {
        assert!(ext(b".md", b".md"));
        assert!(ext(b".MD", b".md"));
    }

    #[test]
    fn non_ascii_bytes_pass_through_unchanged() {
        // "café.JPG" tail ".JPG" folds to ".jpg"; non-ascii bytes untouched.
        assert!(ext("café.JPG".as_bytes(), b".jpg"));
    }
}
