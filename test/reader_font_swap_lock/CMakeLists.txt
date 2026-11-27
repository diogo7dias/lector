add_executable(ReaderFontSwapLockTest ReaderFontSwapLockTest.cpp)

target_link_libraries(ReaderFontSwapLockTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

target_compile_definitions(ReaderFontSwapLockTest PRIVATE
  READER_SOURCE_DIR="${REPO_ROOT}/src/activities/reader"
)

gtest_discover_tests(ReaderFontSwapLockTest)
