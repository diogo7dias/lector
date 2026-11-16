# JPEGDEC's gray decode against libjpeg, on the device's decoder build (see fuzz/image).
FetchContent_MakeAvailable(jpegdec)

add_executable(JpegDecodeAccuracyTest
  JpegDecodeAccuracyTest.cpp
  ${jpegdec_SOURCE_DIR}/src/JPEGDEC.cpp
)

target_include_directories(JpegDecodeAccuracyTest PRIVATE ${jpegdec_SOURCE_DIR}/src)
target_compile_definitions(JpegDecodeAccuracyTest PRIVATE __LINUX__ NO_SIMD LECTOR_DEVICE_REGISTER_WIDTH
  FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
# The vendored decoder is not built warning-clean; only the test's own unit gets the warnings.
set_source_files_properties(${jpegdec_SOURCE_DIR}/src/JPEGDEC.cpp TARGET_DIRECTORY JpegDecodeAccuracyTest
  PROPERTIES COMPILE_OPTIONS -w)

target_link_libraries(JpegDecodeAccuracyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(JpegDecodeAccuracyTest)
