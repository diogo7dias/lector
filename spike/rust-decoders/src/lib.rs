//! Measurement spike: pure-Rust JPEG, PNG and deflate decoders behind a small C ABI, so the
//! bench in `bench/` can run them next to JPEGDEC, PNGdec, uzlib and tinfl on the same images.
//! Not wired into the firmware.
#![cfg_attr(not(feature = "std"), no_std)]

extern crate alloc;

use alloc::boxed::Box;
use alloc::vec;
use core::alloc::{GlobalAlloc, Layout};
use core::ffi::c_void;
use core::sync::atomic::{AtomicUsize, Ordering};

use miniz_oxide::inflate::TINFLStatus;
use miniz_oxide::inflate::core::{DecompressorOxide, decompress};
use zune_core::bytestream::ZCursor;
use zune_core::colorspace::ColorSpace;
use zune_core::options::DecoderOptions;
use zune_core::result::DecodingResult;

pub const RDEC_OK: i32 = 0;
pub const RDEC_ERR_HEADER: i32 = -1;
pub const RDEC_ERR_DECODE: i32 = -2;
pub const RDEC_ERR_TOO_BIG: i32 = -3;
pub const RDEC_ERR_FORMAT: i32 = -4;

/// Called once per output row, top to bottom: `gray` holds `width` 8-bit samples.
pub type RowCb = unsafe extern "C" fn(ctx: *mut c_void, y: i32, gray: *const u8, width: i32);

// The C3 has no atomic read-modify-write (rv32imc), so these are plain load/store pairs; the
// bench calls the decoders from one task only.
static CUR: AtomicUsize = AtomicUsize::new(0);
static PEAK: AtomicUsize = AtomicUsize::new(0);

fn add(n: usize) {
    let c = CUR.load(Ordering::Relaxed) + n;
    CUR.store(c, Ordering::Relaxed);
    if c > PEAK.load(Ordering::Relaxed) {
        PEAK.store(c, Ordering::Relaxed);
    }
}

fn sub(n: usize) {
    CUR.store(CUR.load(Ordering::Relaxed) - n, Ordering::Relaxed);
}

#[cfg(not(feature = "std"))]
mod sys {
    use core::ffi::c_void;
    unsafe extern "C" {
        pub fn malloc(size: usize) -> *mut c_void;
        pub fn memalign(align: usize, size: usize) -> *mut c_void;
        pub fn free(p: *mut c_void);
    }
}

/// Counts live and peak bytes as requested by the Rust code (allocator headers excluded).
struct Counting;

unsafe impl GlobalAlloc for Counting {
    unsafe fn alloc(&self, l: Layout) -> *mut u8 {
        #[cfg(not(feature = "std"))]
        let p = unsafe {
            if l.align() <= 4 {
                sys::malloc(l.size())
            } else {
                sys::memalign(l.align(), l.size())
            }
        } as *mut u8;
        #[cfg(feature = "std")]
        let p = unsafe { std::alloc::System.alloc(l) };
        if !p.is_null() {
            add(l.size());
        }
        p
    }

    unsafe fn dealloc(&self, p: *mut u8, l: Layout) {
        sub(l.size());
        #[cfg(not(feature = "std"))]
        unsafe {
            sys::free(p as *mut c_void)
        };
        #[cfg(feature = "std")]
        unsafe {
            std::alloc::System.dealloc(p, l)
        };
    }
}

#[global_allocator]
static GLOBAL: Counting = Counting;

#[cfg(not(feature = "std"))]
#[panic_handler]
fn panic(info: &core::panic::PanicInfo) -> ! {
    use core::fmt::Write;
    unsafe extern "C" {
        fn rdec_on_panic(msg: *const u8) -> !;
    }
    // A fixed buffer: the panic may be an allocation failure, so nothing here allocates.
    struct Buf([u8; 160], usize);
    impl Write for Buf {
        fn write_str(&mut self, s: &str) -> core::fmt::Result {
            for b in s.bytes() {
                if self.1 + 1 < self.0.len() {
                    self.0[self.1] = b;
                    self.1 += 1;
                }
            }
            Ok(())
        }
    }
    let mut buf = Buf([0; 160], 0);
    let _ = write!(buf, "{}", info);
    unsafe { rdec_on_panic(buf.0.as_ptr()) }
}

#[unsafe(no_mangle)]
pub extern "C" fn rdec_heap_reset() {
    PEAK.store(CUR.load(Ordering::Relaxed), Ordering::Relaxed);
}

#[unsafe(no_mangle)]
pub extern "C" fn rdec_heap_peak() -> usize {
    PEAK.load(Ordering::Relaxed)
}

fn limits() -> DecoderOptions {
    // The firmware's own ceiling for decoded images (ImageDimsProbe caps at 2048 px wide).
    DecoderOptions::default()
        .set_max_width(4096)
        .set_max_height(4096)
}

unsafe fn slice<'a>(data: *const u8, len: usize) -> &'a [u8] {
    if data.is_null() || len == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(data, len) }
    }
}

/// Decodes a baseline or progressive JPEG at full scale to luma rows.
///
/// # Safety
/// `data` must point to `len` readable bytes; `w`/`h` must be writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rdec_jpeg_gray(
    data: *const u8,
    len: usize,
    cb: RowCb,
    ctx: *mut c_void,
    w: *mut i32,
    h: *mut i32,
) -> i32 {
    let bytes = unsafe { slice(data, len) };
    let opts = limits().jpeg_set_out_colorspace(ColorSpace::Luma);
    let mut dec = zune_jpeg::JpegDecoder::new_with_options(ZCursor::new(bytes), opts);
    if dec.decode_headers().is_err() {
        return RDEC_ERR_HEADER;
    }
    let Some(info) = dec.info() else {
        return RDEC_ERR_HEADER;
    };
    let (width, height) = (info.width as usize, info.height as usize);
    let Some(size) = dec.output_buffer_size() else {
        return RDEC_ERR_TOO_BIG;
    };
    let mut out = vec![0u8; size];
    if dec.decode_into(&mut out).is_err() {
        return RDEC_ERR_DECODE;
    }
    unsafe {
        *w = width as i32;
        *h = height as i32;
    }
    for (y, row) in out.chunks_exact(width).take(height).enumerate() {
        unsafe { cb(ctx, y as i32, row.as_ptr(), width as i32) };
    }
    RDEC_OK
}

fn gray_of(r: u32, g: u32, b: u32) -> u32 {
    (r * 77 + g * 150 + b * 29) >> 8
}

// Same blend as PngToFramebufferConverter's convertLineToGray: composite over white.
fn over_white(gray: u32, alpha: u32) -> u8 {
    ((gray * alpha + 255 * (255 - alpha)) / 255) as u8
}

/// Decodes a PNG to 8-bit gray rows with the firmware's gray formula and white matte.
///
/// # Safety
/// `data` must point to `len` readable bytes; `w`/`h` must be writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rdec_png_gray(
    data: *const u8,
    len: usize,
    cb: RowCb,
    ctx: *mut c_void,
    w: *mut i32,
    h: *mut i32,
) -> i32 {
    let bytes = unsafe { slice(data, len) };
    let mut dec = zune_png::PngDecoder::new_with_options(ZCursor::new(bytes), limits());
    if dec.decode_headers().is_err() {
        return RDEC_ERR_HEADER;
    }
    let Some((width, height)) = dec.dimensions() else {
        return RDEC_ERR_HEADER;
    };
    let comps = match dec.colorspace() {
        Some(ColorSpace::Luma) => 1,
        Some(ColorSpace::LumaA) => 2,
        Some(ColorSpace::RGB) => 3,
        Some(ColorSpace::RGBA) => 4,
        _ => return RDEC_ERR_FORMAT,
    };
    let pixels = match dec.decode() {
        Ok(DecodingResult::U8(v)) => v,
        Ok(DecodingResult::U16(v)) => v.iter().map(|s| (s >> 8) as u8).collect(),
        _ => return RDEC_ERR_DECODE,
    };
    if pixels.len() < width * height * comps {
        return RDEC_ERR_DECODE;
    }
    unsafe {
        *w = width as i32;
        *h = height as i32;
    }
    let mut line = vec![0u8; width];
    for (y, src) in pixels.chunks_exact(width * comps).take(height).enumerate() {
        for (x, p) in src.chunks_exact(comps).enumerate() {
            let p: [u32; 4] = core::array::from_fn(|i| *p.get(i).unwrap_or(&255) as u32);
            line[x] = match comps {
                1 => p[0] as u8,
                2 => over_white(p[0], p[1]),
                3 => gray_of(p[0], p[1], p[2]) as u8,
                _ => over_white(gray_of(p[0], p[1], p[2]), p[3]),
            };
        }
        unsafe { cb(ctx, y as i32, line.as_ptr(), width as i32) };
    }
    RDEC_OK
}

const WINDOW: usize = 32 * 1024;

/// Inflates a raw deflate stream (zip entry style) through a 32 KB ring, as lib/miniz's
/// InflateStream does with tinfl. Returns the decompressed length, or a negative error;
/// `*sum` gets a byte sum of the output so the bench can compare against the C decoders.
///
/// # Safety
/// `data` must point to `len` readable bytes; `sum` must be writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rdec_inflate_raw(data: *const u8, len: usize, sum: *mut u32) -> i64 {
    let mut input = unsafe { slice(data, len) };
    let mut state = Box::new(DecompressorOxide::new());
    let mut ring = vec![0u8; WINDOW];
    let (mut pos, mut total, mut acc) = (0usize, 0i64, 0u32);
    loop {
        let (status, used, produced) = decompress(&mut state, input, &mut ring, pos, 0);
        input = &input[used..];
        for b in &ring[pos..pos + produced] {
            acc = acc.wrapping_add(*b as u32);
        }
        total += produced as i64;
        pos = (pos + produced) & (WINDOW - 1);
        match status {
            TINFLStatus::Done => break,
            TINFLStatus::HasMoreOutput => continue,
            TINFLStatus::NeedsMoreInput if used > 0 || produced > 0 => continue,
            _ => return RDEC_ERR_DECODE as i64,
        }
    }
    unsafe { *sum = acc };
    total
}
