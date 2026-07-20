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

/// Directories-first natural ordering (mirror of C++ `FsHelpers::naturalFileLess`):
/// a name ending in '/' is a directory and sorts before any file; within the same
/// kind, order by `natural_less`.
pub fn natural_file_less(a: &[u8], b: &[u8]) -> bool {
    let is_dir1 = a.last() == Some(&b'/');
    let is_dir2 = b.last() == Some(&b'/');
    if is_dir1 != is_dir2 {
        return is_dir1;
    }
    natural_less(a, b)
}

/// FFI entry point for `natural_file_less`. Null pointers read as empty slices.
///
/// # Safety
/// `a_ptr`/`b_ptr` must each point to at least `a_len`/`b_len` bytes, or be null.
#[no_mangle]
pub unsafe extern "C" fn fshelpers_natural_file_less(
    a_ptr: *const u8,
    a_len: usize,
    b_ptr: *const u8,
    b_len: usize,
) -> bool {
    natural_file_less(slice_or_empty(a_ptr, a_len), slice_or_empty(b_ptr, b_len))
}

// ---- UTF-8 decode (byte-exact mirror of lib/Utf8/Utf8.cpp) ----------------

const REPLACEMENT_GLYPH: u32 = 0xFFFD;

/// Mirror of C++ `utf8CodepointLen`: byte count implied by a lead byte
/// (1 for ASCII or any invalid lead).
#[inline]
fn utf8_codepoint_len(c: u8) -> usize {
    if c < 0x80 {
        1
    } else if c >> 5 == 0x6 {
        2
    } else if c >> 4 == 0xE {
        3
    } else if c >> 3 == 0x1E {
        4
    } else {
        1
    }
}

/// Bounds-safe mirror of C++ `utf8NextCodepoint`. `i` indexes into `s` and is
/// advanced past the consumed bytes. Any index at or beyond the slice end reads
/// as 0, matching how the C++ walks a NUL-terminated `c_str()` (the terminator
/// fails the continuation check). Never reads out of bounds.
fn utf8_next_codepoint(s: &[u8], i: &mut usize) -> u32 {
    let at = |idx: usize| -> u8 { if idx < s.len() { s[idx] } else { 0 } };
    let lead = at(*i);
    if lead == 0 {
        return 0;
    }
    let bytes = utf8_codepoint_len(lead);
    if bytes == 1 && lead >= 0x80 {
        *i += 1;
        return REPLACEMENT_GLYPH;
    }
    if bytes == 1 {
        *i += 1;
        return lead as u32;
    }
    let start = *i;
    for k in 1..bytes {
        if at(start + k) & 0xC0 != 0x80 {
            *i += k;
            return REPLACEMENT_GLYPH;
        }
    }
    let mask = (1u32 << (7 - bytes)) - 1;
    let mut cp = (lead as u32) & mask;
    for k in 1..bytes {
        cp = (cp << 6) | (at(start + k) & 0x3F) as u32;
    }
    let overlong =
        (bytes == 2 && cp < 0x80) || (bytes == 3 && cp < 0x800) || (bytes == 4 && cp < 0x10000);
    let surrogate = (0xD800..=0xDFFF).contains(&cp);
    if overlong || surrogate || cp > 0x10FFFF {
        *i += 1;
        return REPLACEMENT_GLYPH;
    }
    *i += bytes;
    cp
}

/// Sanitize a filename into the caller-provided `out` buffer; returns the number
/// of bytes written. Byte-exact mirror of C++ `StringUtils::sanitizeFilename`:
/// skip leading spaces/dots, replace the reserved set (/ \ : * ? " < > |) with
/// '_', drop control characters, keep printable ASCII and UTF-8, cap the byte
/// budget at `max_bytes`, trim trailing spaces/dots, and fall back to "book"
/// when the result is empty. `out` must have capacity >= max(max_bytes, 4).
pub fn sanitize_filename(name: &[u8], max_bytes: usize, out: &mut [u8]) -> usize {
    let mut len = 0usize;
    let mut i = 0usize;

    // Skip leading spaces and dots so they do not consume the byte budget.
    while i < name.len() && (name[i] == b' ' || name[i] == b'.') {
        i += 1;
    }

    // Walk whole UTF-8 codepoints; stop at a NUL (matching C++ c_str semantics).
    while i < name.len() && name[i] != 0 {
        let cp_start = i;
        let cp = utf8_next_codepoint(name, &mut i);
        if matches!(
            cp,
            0x2F | 0x5C | 0x3A | 0x2A | 0x3F | 0x22 | 0x3C | 0x3E | 0x7C
        ) {
            // Reserved filename characters -> '_'
            if len + 1 > max_bytes {
                break;
            }
            if len < out.len() {
                out[len] = b'_';
            }
            len += 1;
        } else if cp >= 128 || (32..127).contains(&cp) {
            // Printable ASCII or any non-ASCII: append the original bytes.
            let cp_bytes = i - cp_start;
            if len + cp_bytes > max_bytes {
                break;
            }
            let mut k = 0;
            while k < cp_bytes {
                if len < out.len() {
                    out[len] = name[cp_start + k];
                }
                len += 1;
                k += 1;
            }
        }
        // else: control characters are dropped.
    }

    // Trim trailing spaces and dots.
    while len > 0 && (out[len - 1] == b' ' || out[len - 1] == b'.') {
        len -= 1;
    }

    if len == 0 {
        let book = b"book";
        let n = book.len().min(out.len());
        out[..n].copy_from_slice(&book[..n]);
        return book.len();
    }
    len
}

/// FFI entry point for `sanitize_filename`. Writes up to `out_cap` bytes at
/// `out_ptr` and returns the result length. Null in/out read as empty.
///
/// # Safety
/// `name_ptr` must point to `name_len` bytes (or be null); `out_ptr` must point
/// to `out_cap` writable bytes (or be null). Caller should size `out_cap` to at
/// least max(max_bytes, 4).
#[no_mangle]
pub unsafe extern "C" fn fshelpers_sanitize_filename(
    name_ptr: *const u8,
    name_len: usize,
    max_bytes: usize,
    out_ptr: *mut u8,
    out_cap: usize,
) -> usize {
    let name = slice_or_empty(name_ptr, name_len);
    let out: &mut [u8] = if out_ptr.is_null() || out_cap == 0 {
        &mut []
    } else {
        core::slice::from_raw_parts_mut(out_ptr, out_cap)
    };
    sanitize_filename(name, max_bytes, out)
}

// ---------------------------------------------------------------------------
// PNG scanline unfilter (crash-prone: raw index math on untrusted image bytes)
// ---------------------------------------------------------------------------

/// PNG Paeth predictor. Byte-exact mirror of C++ `paethPredictor`.
#[inline]
fn paeth_predictor(a: u8, b: u8, c: u8) -> u8 {
    let p = a as i32 + b as i32 - c as i32;
    let pa = (p - a as i32).abs();
    let pb = (p - b as i32).abs();
    let pc = (p - c as i32).abs();
    if pa <= pb && pa <= pc {
        a
    } else if pb <= pc {
        b
    } else {
        c
    }
}

/// Reverse one PNG scanline filter in place. `cur` is the filtered row (modified
/// to the reconstructed row); `prev` is the already-reconstructed row above (all
/// zero for the first row); `bpp` is the byte step between a pixel and its left
/// neighbour. `filter` is 0=None 1=Sub 2=Up 3=Average 4=Paeth.
///
/// Byte-exact mirror of C++ `decodeScanline`'s reverse-filter switch for any
/// valid input (`bpp >= 1`, `prev.len() >= cur.len()`), but every access is
/// bounds-checked: a malformed image that mis-sizes rows or sends `bpp == 0`
/// yields a clean `false` here instead of the out-of-bounds read / underflow a
/// raw C++ pointer loop would take on the device. Returns `false` on an unknown
/// filter or `bpp == 0`, matching a decode failure.
pub fn png_unfilter_row(filter: u8, cur: &mut [u8], prev: &[u8], bpp: usize) -> bool {
    let n = cur.len();
    #[inline]
    fn left(cur: &[u8], i: usize, bpp: usize) -> u8 {
        if i >= bpp { cur[i - bpp] } else { 0 }
    }
    #[inline]
    fn up(prev: &[u8], i: usize) -> u8 {
        if i < prev.len() { prev[i] } else { 0 }
    }
    #[inline]
    fn up_left(prev: &[u8], i: usize, bpp: usize) -> u8 {
        if i >= bpp && (i - bpp) < prev.len() { prev[i - bpp] } else { 0 }
    }

    match filter {
        0 => true, // None
        1 => {
            // Sub
            if bpp == 0 {
                return false;
            }
            for i in bpp..n {
                cur[i] = cur[i].wrapping_add(cur[i - bpp]);
            }
            true
        }
        2 => {
            // Up
            for i in 0..n {
                cur[i] = cur[i].wrapping_add(up(prev, i));
            }
            true
        }
        3 => {
            // Average
            if bpp == 0 {
                return false;
            }
            for i in 0..n {
                let a = left(cur, i, bpp) as u16;
                let b = up(prev, i) as u16;
                cur[i] = cur[i].wrapping_add(((a + b) / 2) as u8);
            }
            true
        }
        4 => {
            // Paeth
            if bpp == 0 {
                return false;
            }
            for i in 0..n {
                let a = left(cur, i, bpp);
                let b = up(prev, i);
                let c = up_left(prev, i, bpp);
                cur[i] = cur[i].wrapping_add(paeth_predictor(a, b, c));
            }
            true
        }
        _ => false,
    }
}

/// FFI entry point for [`png_unfilter_row`]. `cur_ptr`/`cur_len` is the in/out
/// row; `prev_ptr`/`prev_len` the row above (may be null/empty for the top row).
///
/// # Safety
/// `cur_ptr` must point to `cur_len` writable bytes (or be null → no-op empty),
/// and `prev_ptr` to `prev_len` readable bytes (or null → empty).
#[no_mangle]
pub unsafe extern "C" fn fshelpers_png_unfilter_row(
    filter: u8,
    cur_ptr: *mut u8,
    cur_len: usize,
    prev_ptr: *const u8,
    prev_len: usize,
    bpp: usize,
) -> bool {
    let cur: &mut [u8] = if cur_ptr.is_null() || cur_len == 0 {
        &mut []
    } else {
        core::slice::from_raw_parts_mut(cur_ptr, cur_len)
    };
    let prev = slice_or_empty(prev_ptr, prev_len);
    png_unfilter_row(filter, cur, prev, bpp)
}

// ---------------------------------------------------------------------------
// BMP row unpack (crash-prone: raw index math on untrusted image bytes)
// ---------------------------------------------------------------------------

/// Unpack one BMP pixel row into `width` 8-bit luminance values. Byte-exact
/// mirror of the `switch(bpp)` unpack in C++ `Bitmap::readNextRow`, for the
/// supported depths 32/24/8/4/2/1, on any input long enough to hold the row.
///
/// 24/32-bit use the same BGR->luma formula `(77*R + 150*G + 29*B) >> 8`; the
/// paletted depths (8/4/2/1) look the pixel's palette index up in `palette`
/// (the 256-entry `paletteLum` table). Every access is bounds-checked: a
/// malformed image whose row is too short for `width`, an unsupported `bpp`, or
/// a palette shorter than 256 entries yields a clean `false` instead of the
/// out-of-bounds read a raw C++ pointer loop would take on the device. On
/// `true`, `out[0..width]` holds the luminance row.
pub fn bmp_unpack_row(row: &[u8], bpp: u16, width: usize, palette: &[u8], out: &mut [u8]) -> bool {
    if width == 0 {
        return true; // empty row: nothing to unpack, matches the C++ no-op
    }
    if out.len() < width {
        return false;
    }
    // Paletted depths index a 256-entry table; anything smaller is malformed.
    if matches!(bpp, 8 | 4 | 2 | 1) && palette.len() < 256 {
        return false;
    }
    match bpp {
        32 => {
            let need = match width.checked_mul(4) {
                Some(v) => v,
                None => return false,
            };
            if row.len() < need {
                return false;
            }
            for x in 0..width {
                let b = x * 4;
                out[x] = ((77u32 * row[b + 2] as u32 + 150u32 * row[b + 1] as u32 + 29u32 * row[b] as u32) >> 8)
                    as u8;
            }
            true
        }
        24 => {
            let need = match width.checked_mul(3) {
                Some(v) => v,
                None => return false,
            };
            if row.len() < need {
                return false;
            }
            for x in 0..width {
                let b = x * 3;
                out[x] = ((77u32 * row[b + 2] as u32 + 150u32 * row[b + 1] as u32 + 29u32 * row[b] as u32) >> 8)
                    as u8;
            }
            true
        }
        8 => {
            if row.len() < width {
                return false;
            }
            for x in 0..width {
                out[x] = palette[row[x] as usize];
            }
            true
        }
        4 => {
            if row.len() < width.div_ceil(2) {
                return false;
            }
            for x in 0..width {
                let byte = row[x >> 1];
                let nibble = if x & 1 == 1 { byte & 0x0F } else { byte >> 4 };
                out[x] = palette[nibble as usize];
            }
            true
        }
        2 => {
            if row.len() < width.div_ceil(4) {
                return false;
            }
            for x in 0..width {
                let idx = (row[x >> 2] >> (6 - ((x & 3) * 2))) & 0x03;
                out[x] = palette[idx as usize];
            }
            true
        }
        1 => {
            if row.len() < width.div_ceil(8) {
                return false;
            }
            for x in 0..width {
                let pal_index = usize::from(row[x >> 3] & (0x80 >> (x & 7)) != 0);
                out[x] = palette[pal_index];
            }
            true
        }
        _ => false,
    }
}

/// FFI entry point for [`bmp_unpack_row`]. `row_ptr`/`row_len` is the raw pixel
/// row; `palette_ptr`/`palette_len` the 256-entry luminance table (may be
/// null/empty for 24/32-bit); `out_ptr`/`out_cap` the caller-owned output
/// (needs `width` bytes).
///
/// # Safety
/// `row_ptr` must point to `row_len` readable bytes (or be null -> empty),
/// `palette_ptr` to `palette_len` readable bytes (or null -> empty), and
/// `out_ptr` to `out_cap` writable bytes (or null -> empty).
#[no_mangle]
pub unsafe extern "C" fn fshelpers_bmp_unpack_row(
    row_ptr: *const u8,
    row_len: usize,
    bpp: u16,
    width: usize,
    palette_ptr: *const u8,
    palette_len: usize,
    out_ptr: *mut u8,
    out_cap: usize,
) -> bool {
    let row = slice_or_empty(row_ptr, row_len);
    let palette = slice_or_empty(palette_ptr, palette_len);
    let out: &mut [u8] = if out_ptr.is_null() || out_cap == 0 {
        &mut []
    } else {
        core::slice::from_raw_parts_mut(out_ptr, out_cap)
    };
    bmp_unpack_row(row, bpp, width, palette, out)
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
    use super::natural_file_less as nfl;
    use super::natural_less as nl;
    use super::{paeth_predictor, png_unfilter_row};
    use super::bmp_unpack_row;

    #[test]
    fn paeth_matches_spec() {
        // pick a: equal distances -> a wins the tie
        assert_eq!(paeth_predictor(10, 20, 15), 15); // p=15, pa=5 pb=5 pc=0 -> c
        assert_eq!(paeth_predictor(0, 0, 0), 0);
        assert_eq!(paeth_predictor(255, 0, 0), 255); // p=255 -> a
    }

    #[test]
    fn unfilter_none_is_identity() {
        let mut row = [1u8, 2, 3, 4];
        assert!(png_unfilter_row(0, &mut row, &[0; 4], 1));
        assert_eq!(row, [1, 2, 3, 4]);
    }

    #[test]
    fn unfilter_sub_reverses_sub() {
        // Sub filter encodes cur[i] -= cur[i-bpp]; unfilter adds it back.
        let orig = [10u8, 20, 35, 60];
        let bpp = 1;
        let mut filt = orig;
        for i in (bpp..filt.len()).rev() {
            filt[i] = filt[i].wrapping_sub(filt[i - bpp]);
        }
        assert!(png_unfilter_row(1, &mut filt, &[0; 4], bpp));
        assert_eq!(filt, orig);
    }

    #[test]
    fn unfilter_up_reverses_up() {
        let prev = [5u8, 9, 2, 200];
        let orig = [10u8, 20, 250, 60];
        let mut filt = orig;
        for i in 0..filt.len() {
            filt[i] = filt[i].wrapping_sub(prev[i]);
        }
        assert!(png_unfilter_row(2, &mut filt, &prev, 3));
        assert_eq!(filt, orig);
    }

    #[test]
    fn unfilter_rejects_unknown_filter_and_zero_bpp() {
        let mut row = [1u8, 2, 3];
        assert!(!png_unfilter_row(9, &mut row, &[0; 3], 1)); // unknown filter
        assert!(!png_unfilter_row(1, &mut row, &[0; 3], 0)); // Sub with bpp 0
        assert!(!png_unfilter_row(4, &mut row, &[0; 3], 0)); // Paeth with bpp 0
    }

    #[test]
    fn unfilter_safe_when_prev_too_short() {
        // A malformed image can hand us a short/empty prev row: must not panic,
        // missing prev bytes read as 0.
        let mut row = [7u8, 7, 7, 7];
        assert!(png_unfilter_row(2, &mut row, &[], 1)); // Up with empty prev -> +0
        assert_eq!(row, [7, 7, 7, 7]);
        let mut row2 = [7u8, 7, 7, 7];
        assert!(png_unfilter_row(4, &mut row2, &[1u8], 2)); // Paeth, prev len 1
    }

    #[test]
    fn unfilter_empty_row_ok() {
        assert!(png_unfilter_row(4, &mut [], &[], 3));
    }

    #[test]
    fn dir_sorts_before_file() {
        assert!(nfl(b"zebra/", b"apple")); // dir before file regardless of name
        assert!(!nfl(b"apple", b"zebra/"));
    }

    #[test]
    fn same_kind_uses_natural_order() {
        assert!(nfl(b"file2", b"file10")); // both files -> numeric-aware
        assert!(nfl(b"dir2/", b"dir10/")); // both dirs -> numeric-aware
        assert!(!nfl(b"file10", b"file2"));
    }

    fn san(name: &[u8], max: usize) -> std::string::String {
        let mut buf = [0u8; 256];
        let n = super::sanitize_filename(name, max, &mut buf);
        std::string::String::from_utf8_lossy(&buf[..n]).into_owned()
    }

    #[test]
    fn sanitize_replaces_illegal_chars() {
        assert_eq!(san(b"a/b:c*d", 100), "a_b_c_d");
        assert_eq!(san(b"a\\b?c\"d<e>f|g", 100), "a_b_c_d_e_f_g");
    }

    #[test]
    fn sanitize_trims_leading_and_trailing() {
        assert_eq!(san(b"  ..name", 100), "name");
        assert_eq!(san(b"name.. ", 100), "name");
    }

    #[test]
    fn sanitize_empty_falls_back_to_book() {
        assert_eq!(san(b"", 100), "book");
        assert_eq!(san(b"...   ", 100), "book");
    }

    #[test]
    fn sanitize_drops_control_chars() {
        assert_eq!(san(b"a\x01b\x1fc", 100), "abc");
    }

    #[test]
    fn sanitize_keeps_utf8() {
        assert_eq!(san("café".as_bytes(), 100), "café");
    }

    #[test]
    fn sanitize_caps_at_max_bytes() {
        assert_eq!(san(b"abcdefghij", 4), "abcd");
    }

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

    // --- bmp_unpack_row ---
    fn ramp_palette() -> [u8; 256] {
        let mut p = [0u8; 256];
        for (i, v) in p.iter_mut().enumerate() {
            *v = i as u8;
        }
        p
    }

    #[test]
    fn bmp_32_bgr_luma() {
        // one pixel B=10 G=20 R=30 X=0 -> (77*30 + 150*20 + 29*10)/256
        let row = [10u8, 20, 30, 0];
        let mut out = [0u8; 1];
        assert!(bmp_unpack_row(&row, 32, 1, &[], &mut out));
        assert_eq!(out[0], ((77u32 * 30 + 150 * 20 + 29 * 10) >> 8) as u8);
    }

    #[test]
    fn bmp_24_bgr_luma() {
        let row = [10u8, 20, 30];
        let mut out = [0u8; 1];
        assert!(bmp_unpack_row(&row, 24, 1, &[], &mut out));
        assert_eq!(out[0], ((77u32 * 30 + 150 * 20 + 29 * 10) >> 8) as u8);
    }

    #[test]
    fn bmp_8_palette() {
        let pal = ramp_palette();
        let row = [0u8, 5, 255, 128];
        let mut out = [0u8; 4];
        assert!(bmp_unpack_row(&row, 8, 4, &pal, &mut out));
        assert_eq!(out, [0, 5, 255, 128]);
    }

    #[test]
    fn bmp_4_nibbles_high_then_low() {
        let pal = ramp_palette();
        let row = [0xAB]; // x0 -> high nibble 0xA, x1 -> low nibble 0xB
        let mut out = [0u8; 2];
        assert!(bmp_unpack_row(&row, 4, 2, &pal, &mut out));
        assert_eq!(out, [0x0A, 0x0B]);
    }

    #[test]
    fn bmp_2_bits() {
        let pal = ramp_palette();
        let row = [0b11_10_01_00]; // x0=3 x1=2 x2=1 x3=0 (MSB first)
        let mut out = [0u8; 4];
        assert!(bmp_unpack_row(&row, 2, 4, &pal, &mut out));
        assert_eq!(out, [3, 2, 1, 0]);
    }

    #[test]
    fn bmp_1_bit_msb_first() {
        let pal = ramp_palette();
        let row = [0b1010_0000]; // x0=1 x1=0 x2=1 x3=0 ...
        let mut out = [0u8; 4];
        assert!(bmp_unpack_row(&row, 1, 4, &pal, &mut out));
        assert_eq!(out, [1, 0, 1, 0]);
    }

    #[test]
    fn bmp_bad_bpp_returns_false() {
        let mut out = [0u8; 4];
        assert!(!bmp_unpack_row(&[0u8; 16], 16, 4, &[], &mut out));
    }

    #[test]
    fn bmp_short_row_returns_false() {
        // 24-bit needs 3*width bytes; give too few.
        let mut out = [0u8; 4];
        assert!(!bmp_unpack_row(&[0u8; 5], 24, 4, &[], &mut out));
    }

    #[test]
    fn bmp_short_out_returns_false() {
        let mut out = [0u8; 1];
        assert!(!bmp_unpack_row(&[0u8; 16], 8, 4, &ramp_palette(), &mut out));
    }

    #[test]
    fn bmp_palette_too_small_returns_false() {
        let mut out = [0u8; 4];
        assert!(!bmp_unpack_row(&[0u8; 4], 8, 4, &[0u8; 100], &mut out));
    }

    #[test]
    fn bmp_zero_width_is_noop_true() {
        let mut out = [0u8; 0];
        assert!(bmp_unpack_row(&[], 8, 0, &[], &mut out));
    }
}
