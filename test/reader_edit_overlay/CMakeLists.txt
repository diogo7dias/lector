add_executable(ReaderEditOverlayTest
  ReaderEditOverlayTest.cpp
)

target_compile_definitions(ReaderEditOverlayTest PRIVATE
  EPUB_READER_SOURCE="${REPO_ROOT}/src/activities/reader/EpubReaderActivity.cpp"
)

target_link_libraries(ReaderEditOverlayTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReaderEditOverlayTest)
