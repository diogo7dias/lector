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

/// Numeric-aware, case-insensitive natural comparison. Returns true when `a`
/// orders strictly before `b`. Byte-exact mirror of C++ `FsHelpers::naturalLess`:
/// digit runs are compared by significant length (leading zeros skipped) then
/// digit-by-digit; other bytes are compared ASCII-case-insensitively. All slice
/// accesses are bounds-checked (no out-of-range indexing).
pub fn natural_less(a: &[u8], b: &[u8]) -> bool {
    let (mut p1, mut p2) = (0usize, 0usize);
    while p1 < a.len() && p2 < b.len() {
        if a[p1].is_ascii_digit() && b[p2].is_ascii_digit() {
            // Skip leading zeros so magnitude, not width, decides.
            while p1 < a.len() && a[p1] == b'0' {
                p1 += 1;
            }
            while p2 < b.len() && b[p2] == b'0' {
                p2 += 1;
            }
            let mut e1 = p1;
            while e1 < a.len() && a[e1].is_ascii_digit() {
                e1 += 1;
            }
            let mut e2 = p2;
            while e2 < b.len() && b[e2].is_ascii_digit() {
                e2 += 1;
            }
            let len1 = e1 - p1;
            let len2 = e2 - p2;
            if len1 != len2 {
                return len1 < len2;
            }
            for i in 0..len1 {
                if a[p1 + i] != b[p2 + i] {
                    return a[p1 + i] < b[p2 + i];
                }
            }
            p1 = e1;
            p2 = e2;
        } else {
            let c1 = a[p1].to_ascii_lowercase();
            let c2 = b[p2].to_ascii_lowercase();
            if c1 != c2 {
                return c1 < c2;
            }
            p1 += 1;
            p2 += 1;
        }
    }
    // a is a strict prefix of b -> a orders first.
    p1 == a.len() && p2 != b.len()
}

/// FFI entry point for `natural_less`. Null pointers read as empty slices.
///
/// # Safety
/// `a_ptr`/`b_ptr` must each point to at least `a_len`/`b_len` bytes, or be null.
#[no_mangle]
pub unsafe extern "C" fn fshelpers_natural_less(
    a_ptr: *const u8,
    a_len: usize,
    b_ptr: *const u8,
    b_len: usize,
) -> bool {
    natural_less(slice_or_empty(a_ptr, a_len), slice_or_empty(b_ptr, b_len))
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
    use super::natural_less as nl;

    #[test]
    fn natural_plain_alpha_order() {
        assert!(nl(b"apple", b"banana"));
        assert!(!nl(b"banana", b"apple"));
    }

    #[test]
    fn natural_case_insensitive() {
        assert!(nl(b"Apple", b"banana"));
        assert!(!nl(b"apple", b"Apple")); // equal ignoring case -> not strictly less
    }

    #[test]
    fn natural_numeric_aware() {
        // "2" < "10" numerically, unlike lexicographic where "10" < "2".
        assert!(nl(b"file2", b"file10"));
        assert!(!nl(b"file10", b"file2"));
    }

    #[test]
    fn natural_leading_zeros_ignored() {
        // "007" and "7" have equal significant length -> equal number run.
        assert!(!nl(b"file007", b"file7"));
        assert!(!nl(b"file7", b"file007"));
    }

    #[test]
    fn natural_prefix_is_less() {
        assert!(nl(b"file", b"file1"));
        assert!(!nl(b"file1", b"file"));
    }

    #[test]
    fn natural_equal_is_not_less() {
        assert!(!nl(b"same", b"same"));
        assert!(!nl(b"", b""));
    }

    #[test]
    fn natural_empty_before_nonempty() {
        assert!(nl(b"", b"a"));
        assert!(!nl(b"a", b""));
    }

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
