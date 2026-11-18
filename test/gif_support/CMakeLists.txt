# GIF in books: ImageDimsProbe's GIF header, GifCommon's header walk, and a decode through
# GifToFramebufferConverter on AnimatedGIF as the firmware builds it (fuzz/image stubs).
FetchContent_MakeAvailable(animatedgif)

add_executable(GifSupportTest
  GifSupportTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/GifToFramebufferConverter.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${animatedgif_SOURCE_DIR}/src/AnimatedGIF.cpp
)

target_include_directories(GifSupportTest BEFORE PRIVATE
  ${REPO_ROOT}/test/fuzz/image/stubs
  ${REPO_ROOT}/test/fuzz/common
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/GifSupport
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/GfxRenderer
  ${animatedgif_SOURCE_DIR}/src
)
target_compile_definitions(GifSupportTest PRIVATE __LINUX__ MAX_WIDTH=3072 ALLOWS_UNALIGNED
  LECTOR_DEVICE_REGISTER_WIDTH)
# The vendored decoder is not built warning-clean; only the firmware's own units get the warnings.
# It loads words from byte offsets on purpose (ALLOWS_UNALIGNED), so UBSan's alignment check is
# off in its unit only (as in fuzz/image).
set_source_files_properties(${animatedgif_SOURCE_DIR}/src/AnimatedGIF.cpp TARGET_DIRECTORY GifSupportTest
  PROPERTIES COMPILE_OPTIONS "-w;-fno-sanitize=alignment")

target_link_libraries(GifSupportTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GifSupportTest)
