add_executable(BitmapResampleTest
  BitmapResampleTest.cpp
  ${REPO_ROOT}/lib/GfxRenderer/Bitmap.cpp
)

target_include_directories(BitmapResampleTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(BitmapResampleTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

# The row cache is X4 Pro only (PSRAM); on the host it allocates with malloc.
target_compile_definitions(BitmapResampleTest PRIVATE BITMAP_ROW_CACHE_HOST=1)

gtest_discover_tests(BitmapResampleTest)
