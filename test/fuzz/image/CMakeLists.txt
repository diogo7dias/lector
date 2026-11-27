# Image decode for chapter pages and the PNG viewer: ImageDimsProbe, ImageDecoderFactory and
# the PNG/JPEG/GIF framebuffer converters (scaling, dithering, DirectPixelWriter, PixelCache) on
# the decoder libraries at the firmware's pins, with scripts/{pngdec,jpegdec,animatedgif}_patches
# applied (jpegdec and animatedgif are declared in test/CMakeLists.txt).
include(FetchContent)
file(GLOB PNGDEC_PATCHES ${REPO_ROOT}/scripts/pngdec_patches/*.patch)
list(SORT PNGDEC_PATCHES)
FetchContent_Declare(pngdec
  GIT_REPOSITORY https://github.com/bitbank2/PNGdec.git
  GIT_TAG 9a9c585fd39d148c5517597b02ce490e0fdb1bb4  # 1.1.6, as platformio.ini
  PATCH_COMMAND git checkout -- src COMMAND git apply ${PNGDEC_PATCHES}
  UPDATE_DISCONNECTED ON
  SOURCE_SUBDIR no-cmake
)
FetchContent_MakeAvailable(pngdec jpegdec animatedgif)

lector_add_fuzzer(image
  fuzz_image.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/GifToFramebufferConverter.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDecoderFactory.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/JpegToFramebufferConverter.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/PngToFramebufferConverter.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${pngdec_SOURCE_DIR}/src/PNGdec.cpp
  ${pngdec_SOURCE_DIR}/src/adler32.c
  ${pngdec_SOURCE_DIR}/src/crc32.c
  ${pngdec_SOURCE_DIR}/src/infback.c
  ${pngdec_SOURCE_DIR}/src/inffast.c
  ${pngdec_SOURCE_DIR}/src/inflate.c
  ${pngdec_SOURCE_DIR}/src/inftrees.c
  ${pngdec_SOURCE_DIR}/src/zutil.c
  ${jpegdec_SOURCE_DIR}/src/JPEGDEC.cpp
  ${animatedgif_SOURCE_DIR}/src/AnimatedGIF.cpp
)

target_include_directories(fuzz_image BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/GifSupport
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/GfxRenderer
  ${pngdec_SOURCE_DIR}/src
  ${jpegdec_SOURCE_DIR}/src
  ${animatedgif_SOURCE_DIR}/src
)

# The device's decoder build: one PNG_MAX_BUFFERED_PIXELS for every unit (sizeof(PNG) depends on
# it), JPEGDEC's plain C paths (the C3 has no SIMD), the 32-bit register width of both JPEGDEC and
# AnimatedGIF (the latter with ALLOWS_UNALIGNED, as esp32-hal.h sets on the device) and the
# firmware's MAX_WIDTH.
target_compile_definitions(fuzz_image PRIVATE __LINUX__ PNG_MAX_BUFFERED_PIXELS=16416 NO_SIMD
  LECTOR_DEVICE_REGISTER_WIDTH ALLOWS_UNALIGNED MAX_WIDTH=3072)
target_compile_options(fuzz_image PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)

# The decoders define ALLOWS_UNALIGNED for x86-64 (and here by the flag above) and load
# words from byte offsets on purpose; only that check is off, and only in their own units.
set_source_files_properties(
  ${pngdec_SOURCE_DIR}/src/PNGdec.cpp ${pngdec_SOURCE_DIR}/src/inflate.c ${pngdec_SOURCE_DIR}/src/inffast.c
  TARGET_DIRECTORY fuzz_image PROPERTIES COMPILE_OPTIONS -fno-sanitize=alignment)
# JPEGDEC's IDCT and DC scaling multiply coefficients by quantizers in int; a hostile
# file overflows them, which wraps on the device to wrong pixels and touches no memory.
set_source_files_properties(${jpegdec_SOURCE_DIR}/src/JPEGDEC.cpp
  TARGET_DIRECTORY fuzz_image PROPERTIES COMPILE_OPTIONS "-fno-sanitize=alignment;-fno-sanitize=signed-integer-overflow")
# AnimatedGIF copies and reads words at byte offsets on purpose (ALLOWS_UNALIGNED), as above.
set_source_files_properties(${animatedgif_SOURCE_DIR}/src/AnimatedGIF.cpp
  TARGET_DIRECTORY fuzz_image PROPERTIES COMPILE_OPTIONS -fno-sanitize=alignment)
